#include "pch.h"

#include "Docked.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Equipment.h"
#include "Galaxy.h"
#include "Input.h"
#include "Market.h"
#include "Ships.h"
#include "Text.h"
#include "Video.h"

#include <algorithm>
#include <initializer_list>

namespace Elite
{

namespace
{

constexpr std::uint16_t CREDITS_ON_MESSAGE_LINE = 0x78; // B800:0078

constexpr std::uint8_t FUEL_TENTHS_PER_UNIT = 10;
constexpr std::uint8_t FUEL_UNITS_PER_TENTH = 0x24; // 255 is 7.0 light years
constexpr std::uint16_t FUEL_TEXT = 0x83E9;         // 'Fuel:  n.n Light Years'
constexpr std::uint16_t FUEL_DIGITS = 0x8407;       // FormatDecimal5's five, of which the last two are shown

constexpr std::uint8_t TEXT_LAYOUT = 2;
constexpr std::uint16_t BORDER_COLOR_PORT = 0x3D9;
constexpr std::uint8_t ATTRIBUTE_MASK = 0x7F; // no blinking
constexpr std::uint8_t FRAME_ROW_CHARACTER = 0xCD;
constexpr std::uint8_t FRAME_SIDE_CHARACTER = 0xBA;
constexpr std::uint16_t FRAME_ROW_CELLS = 0x26;
constexpr std::uint16_t FRAME_TOP_ROW = 0x002;    // line 0, column 1
constexpr std::uint16_t FRAME_TITLE_ROW = 0x0A2;  // line 2
constexpr std::uint16_t FRAME_BOTTOM_ROW = 0x782; // line 24
constexpr std::uint16_t FRAME_SIDES = 0x050;      // line 1, column 0
constexpr std::uint16_t FRAME_SIDE_ROWS = 0x17;
constexpr std::uint16_t FRAME_RIGHT_SIDE = 0x4E; // column 39
constexpr std::uint16_t TEXT_ROW_BYTES = 0x50;
constexpr std::uint16_t FRAME_CORNERS = 6;
constexpr std::uint16_t FRAME_CORNER_BYTES = 3; // the cell's offset, then the character

// The routines the docked screens call, each through its hook or the original (ADR-010 item 8).
constexpr std::uint16_t FINISH_SPACE_VIEW_FRAME = 0x0570;
constexpr std::uint16_t SHOW_GALACTIC_CHART = 0x0CAE;
constexpr std::uint16_t SHOW_SHORT_RANGE_CHART = 0x0E52;
constexpr std::uint16_t DRAW_VIEW_STRING = 0x31EC;
constexpr std::uint16_t DRAW_SCREEN_STRING = 0x32D8;
constexpr std::uint16_t CLEAR_MESSAGE_LINE = 0x3609;
constexpr std::uint16_t TRANSFORM_AND_DRAW_OBJECTS = 0x3D25;
constexpr std::uint16_t START_NEW_GAME = 0x4671;
constexpr std::uint16_t CLEAR_ALL_OBJECTS = 0x52B2;
constexpr std::uint16_t SHOW_SELL_CARGO_SCREEN = 0x5A30;
constexpr std::uint16_t SHOW_BUY_CARGO_SCREEN = 0x5AE9;
constexpr std::uint16_t SHOW_EQUIP_SHIP_SCREEN = 0x5BF2;
constexpr std::uint16_t SHOW_SYSTEM_DATA_SCREEN = 0x5CDE;
constexpr std::uint16_t SHOW_MARKET_PRICES_SCREEN = 0x5E2C;
constexpr std::uint16_t SHOW_COMMANDER_STATUS_SCREEN = 0x5EA9;
constexpr std::uint16_t SHOW_INVENTORY_SCREEN = 0x6020;
constexpr std::uint16_t PRINT_TEXT_MODE_STRING = 0x60D2;
constexpr std::uint16_t SHOW_DISC_CONTROL_SCREEN = 0x660B;
constexpr std::uint16_t FORMAT_TENTHS = 0x69B3;
constexpr std::uint16_t NEXT_MARKET_RANDOM = 0x6A85;
constexpr std::uint16_t RUN_CARGO_TRADE_MENU = 0x6B1E;
constexpr std::uint16_t START_MUSIC = 0x7401;
constexpr std::uint16_t STOP_ALL_SOUND = 0x7423;
constexpr std::uint16_t GET_KEY = 0x7616;
constexpr std::uint16_t RESET_KEYBOARD = 0x7668;
constexpr std::uint16_t SHOW_COCKPIT_SCREEN = 0x7BC0;
constexpr std::uint16_t DRAW_TITLE_PLANET = 0x7D4E;
constexpr std::uint16_t SHOW_CREDITS = 0x8F02;

// Where the original jumps back (Guest::LoopTurn).
constexpr std::uint16_t DOCKED_KEY_DISPATCH = 0x0B40;
constexpr std::uint16_t DOCKED_KEY_TEST = 0x0B45; // after a screen, the key it returned
constexpr std::uint16_t DOCKED_STATUS_ENTRY = 0x0B96;
constexpr std::uint16_t DOCKED_DISK_MENU_ENTRY = 0x0BAC;
constexpr std::uint16_t SELL_CARGO_ROW = 0x5A75;
constexpr std::uint16_t BUY_CARGO_ROW = 0x5B40;
constexpr std::uint16_t BUY_CARGO_ROW_END = 0x5BBA;
constexpr std::uint16_t STATUS_CASH_SPACE = 0x5F32;
constexpr std::uint16_t STATUS_RATING = 0x5F70;
constexpr std::uint16_t STATUS_EQUIPMENT_ROW = 0x5FAD;
constexpr std::uint16_t STATUS_LASER_MOUNT = 0x5FD4;
constexpr std::uint16_t INVENTORY_CASH_SPACE = 0x6051;
constexpr std::uint16_t INVENTORY_ROW = 0x6066;
constexpr std::uint16_t INVENTORY_ROW_END = 0x60A1;
constexpr std::uint16_t WAIT_FOR_SCREEN_EXIT_KEY = 0x60B4;
constexpr std::uint16_t SUPERNOVA_ANSWER = 0x6E23;
constexpr std::uint16_t EVACUATE_CARGO = 0x6E4A;
constexpr std::uint16_t MASK_DEBRIEFING_TEXT = 0x6F52;
constexpr std::uint16_t TITLE_FRAME_LOOP = 0x7DF1;

// Scan codes.
constexpr std::uint8_t SCAN_ESCAPE = 0x01;
constexpr std::uint8_t SCAN_Y = 0x15;
constexpr std::uint8_t SCAN_N = 0x31;
constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_F2 = 0x3C;
constexpr std::uint8_t SCAN_F3 = 0x3D;
constexpr std::uint8_t SCAN_F4 = 0x3E;
constexpr std::uint8_t SCAN_F5 = 0x3F;
constexpr std::uint8_t SCAN_F6 = 0x40;
constexpr std::uint8_t SCAN_F7 = 0x41;
constexpr std::uint8_t SCAN_F8 = 0x42;
constexpr std::uint8_t SCAN_F9 = 0x43;
constexpr std::uint8_t SCAN_F10 = 0x44;

// The docked screens' texts and places, in the data segment and on the 40-column text page.
constexpr std::uint16_t TITLE_CELL = 0x54;
constexpr std::uint16_t LINE_3 = 0xF4;
constexpr std::uint16_t LINE_4 = 0x144;
constexpr std::uint16_t LINE_5 = 0x194;
constexpr std::uint16_t LINE_6 = 0x1E4;
constexpr std::uint16_t LINE_7 = 0x234;
constexpr std::uint16_t LINE_8 = 0x284;
constexpr std::uint16_t LINE_9 = 0x2D4;
constexpr std::uint16_t LINE_10 = 0x324;
constexpr std::uint8_t HEADER_ATTRIBUTE = 0x4E;
constexpr std::uint8_t ROW_ATTRIBUTE = 0x4F;
constexpr std::uint8_t STATUS_ATTRIBUTE = 0x0F;
constexpr std::uint8_t EQUIPMENT_HEADING_ATTRIBUTE = 0x0D;
constexpr std::uint16_t SELL_CARGO_HELP_TEXT = 0x8C6B; // 'Use control keys or device to select', then its second line
constexpr std::uint16_t BUY_CARGO_HELP_TEXT = 0x8C25;
constexpr std::uint16_t TRADE_HEADER_TEXT = 0x7BEC; // 'PRODUCT    UNIT    PRICE    QTY'
constexpr std::uint16_t NO_QUANTITY_TEXT = 0x821B;  // '     -'
constexpr std::uint16_t PRODUCT_NAME_END = 0x0C;    // a product name's 12 characters, then a space
constexpr std::uint16_t UNIT_AFTER_NAME = 0x0D;     // and then its unit
constexpr std::uint16_t PRODUCT_NAME_BYTES = 0x11;
constexpr std::uint16_t TRADE_ROWS = 0x11;
constexpr std::uint16_t INVENTORY_ROWS = 0x12; // the 17 products and the refugees
constexpr std::uint16_t QUANTITY_DIGITS_BLANKED = 4;
constexpr std::uint8_t QUANTITY_RANDOM_MASK = 0x1F;
constexpr std::uint8_t QUANTITY_RANDOM_BIAS = 7;
constexpr std::uint8_t QUANTITY_RANDOM_HIGH_MASK = 3;
constexpr std::uint16_t SYSTEM_LABEL_TEXT = 0x83CB;      // 'System:'
constexpr std::uint16_t HYPERSYSTEM_LABEL_TEXT = 0x83DA; // 'Hypersystem:'
constexpr std::uint16_t WITCH_SPACE_TEXT = 0x8E30;       // 'WITCH SPACE'
constexpr std::uint16_t CASH_LABEL_TEXT = 0x840C;        // 'Cash:'
constexpr std::uint16_t LEGAL_STATUS_LABEL_TEXT = 0x841B;
constexpr std::uint16_t RATING_LABEL_TEXT = 0x8448;
constexpr std::uint16_t EQUIPMENT_HEADING_TEXT = 0x84C7; // 'EQUIPMENT:'
constexpr std::uint16_t MOUNTS_OPEN_TEXT = 0x8214;       // ' ('
constexpr std::uint16_t MOUNTS_CLOSE_TEXT = 0x8217;      // ')'
constexpr std::uint8_t LEGAL_STATUS_OFFENDER = 1;
constexpr std::uint8_t LEGAL_STATUS_FUGITIVE = 0x28;
constexpr std::uint8_t DIGIT_ZERO = 0x30;
constexpr std::uint16_t EQUIPMENT_ROWS = 0x0D;
constexpr std::uint8_t FIRST_EQUIPMENT_MENU_ROW = 2;
constexpr std::uint16_t LASER_MOUNTS = 4;
constexpr std::uint8_t LASER_MOUNT_TYPE_MASK = 3;
constexpr std::uint16_t INVENTORY_CASH_BACK = 0x10; // the cash eight cells nearer its label than the status screen's
constexpr std::uint16_t INVENTORY_QUANTITY_TEXT = 0x8995;
constexpr std::uint16_t FUEL_LABEL_BYTES = 6;        // 'Fuel: ', where a NUL cuts the fuel text
constexpr std::uint16_t FUEL_DIGITS_AFTER_LABEL = 8; // from that NUL past the padding to 'n.n Light Years'
constexpr std::uint16_t RANK_LETTERS = 9;            // 'ARCHANGEL' over 'COMMANDER'
constexpr std::uint8_t SPACE = 0x20;
constexpr std::uint8_t NUL = 0;

constexpr std::uint8_t MISSION_SUPERNOVA = 1;
constexpr std::uint8_t MISSION_MASK_SHIP = 2;
constexpr std::uint8_t STAGE_BRIEFED = 1;
constexpr std::uint8_t STAGE_DEBRIEFED = 2;
constexpr std::uint16_t SUPERNOVA_BRIEFING_LINES = 5;
constexpr std::uint16_t SUPERNOVA_REFUSED_TEXT = 0x8EB1;  // 'We hope you die a horribly long'
constexpr std::uint16_t SUPERNOVA_ACCEPTED_TEXT = 0x8F79; // 'Thank you!'
constexpr std::uint16_t SUPERNOVA_ANSWER_LINES = 8;
constexpr std::uint8_t REFUGEE_TONNES = 0x14;
constexpr std::uint8_t REFUGEE_TONNES_LARGE_BAY = 0x23;
constexpr std::uint16_t SUPERNOVA_DELAY_FRAMES = 0x190;
constexpr std::uint16_t MASK_BRIEFING_LINES = 9;
constexpr std::uint8_t MASK_SHIPS = 5;
constexpr std::uint8_t MASK_SYSTEM_JUMPS = 2;
constexpr std::uint16_t INVASION_BRIEFING_LINES = 0x0C;
constexpr std::uint8_t REWARD_DIGIT_FULL_HOLD = 0x30; // '1000 Credits' when all 20 tonnes came
constexpr std::uint16_t REWARD_FULL_HOLD_TENTHS = 0x2710;
constexpr std::uint8_t REWARD_DIGIT_OTHERWISE = 0x34; // '1400 Credits'
constexpr std::uint16_t REWARD_OTHERWISE_TENTHS = 0x36B0;
constexpr std::uint16_t SUPERNOVA_DEBRIEFING_TEXT = 0x9056; // 'Thank you for saving us.'
constexpr std::uint16_t SUPERNOVA_DEBRIEFING_LINES = 4;
constexpr std::uint16_t MASK_RECOVERED_TEXT = 0x92B4; // 'This masking device looks very'
constexpr std::uint16_t MASK_RECOVERED_LINES = 8;
constexpr std::uint16_t MASK_FLED_TEXT = 0x925D; // 'Running away from the mask ship'
constexpr std::uint16_t MASK_FLED_LINES = 4;
constexpr std::uint16_t MASK_DESTROYED_TEXT = 0x91A8; // 'It was unfortunate that you could'
constexpr std::uint16_t MASK_DESTROYED_LINES = 7;
constexpr std::uint16_t INVASION_DEBRIEFING_LINES = 0x0E;

constexpr std::uint16_t TITLE_TEXT_CELL = 0x65;
constexpr std::uint16_t WHITE_MASK = 0xFFFF;
constexpr std::uint16_t PRESS_ANY_KEY_PLACE = 0x1C05;
constexpr std::uint16_t PRESS_ANY_KEY_MASK = 0x5555;
constexpr std::uint8_t TITLE_SHIP_SLOTS = 3;
constexpr std::uint8_t TITLE_LAYOUT = 2;
constexpr std::uint16_t TITLE_PLANET_RADIUS = 0x3C;
constexpr std::uint16_t TITLE_PLANET_X = 0xC8;
constexpr std::uint16_t TITLE_PLANET_Y = 0x32;
constexpr std::uint8_t TITLE_PLANET_COLOR = 2;
constexpr std::uint8_t TITLE_FIRST_SHIP_TYPE = 2;
constexpr std::uint8_t TITLE_LAST_SHIP_TYPE = 0x1D;
// The title ship, in stationSlot: what turns it each frame, and its flags.
constexpr std::uint16_t TITLE_PITCH_STEP = 0x19;
constexpr std::uint16_t TITLE_YAW_STEP = 0x14;
constexpr std::uint16_t TITLE_ROLL_STEP = 0x1E;
constexpr std::uint8_t TITLE_SHIP_FLAGS = 2;

// ADD WORD PTR [_offset],_value.
void AddToWord(GameState& _state, std::uint16_t _offset, std::uint16_t _value)
{
  _state.SetWord(_offset, Offset(_state.Word(_offset), _value));
}

// CALL GetKey; JE _loop: GetKey until a key comes.
void WaitForKey(Guest& _guest, std::uint16_t _loop)
{
  for (;;)
  {
    _guest.Call(GET_KEY);
    if (!_guest.Flag(Machine::FLAG_ZERO))
    {
      return;
    }
    _guest.JumpBack(_loop);
  }
}

// CALL GetKey; JE _loop, de-assembled: GetKey until a key comes, each empty turn ending at the jump back (ADR-015). The turns
// carry nothing, as WaitForKeyPress's do. Returns the key.
KeyPress WaitForKey(GameState& _state, Hardware& _hardware, std::uint16_t _loop)
{
  for (;;)
  {
    const KeyPress key = GetKey(_state, _hardware);
    if (key.scanCode != 0)
    {
      return key;
    }
    _hardware.LoopTurn(_loop, {});
  }
}

// The cash without its leading spaces: INC SI from the byte before creditBalanceText while it is on a space, each turn of the
// scan jumping back to _loop with SI. Returns the first character that is not a space.
std::uint16_t SkipCashSpaces(const GameState& _state, Hardware& _hardware, std::uint16_t _loop)
{
  auto cash = static_cast<std::uint16_t>(DS.creditBalanceText.offset - 1);
  for (;;)
  {
    cash = Offset(cash, 1);
    if (_state.Byte(cash) != SPACE)
    {
      return cash;
    }
    _hardware.LoopTurn(_loop, {cash});
  }
}

// What FormatQuantity leaves: FormatDecimal5's units, and where BlankLeadingZeros stopped.
struct FormattedQuantity
{
  std::uint16_t units;
  BlankedZeros blanked;
};

// _quantity as five digits in the text at DS:_text, up to four of its leading zeros blanked.
FormattedQuantity FormatQuantity(GameState& _state, std::uint16_t _quantity, std::uint16_t _text)
{
  const std::uint16_t units = FormatDecimal5(_state, _quantity, _text);
  return FormattedQuantity{units, BlankLeadingZeros(_state, _text, static_cast<std::uint8_t>(QUANTITY_DIGITS_BLANKED))};
}

// FormatQuantity of AX, and the registers as the original leaves them: FormatDecimal5's AX, the units with their digit
// in AL, BlankLeadingZeros' DI and CX, and SI on the text.
void FormatQuantityOnRegisters(Guest& _guest, std::uint16_t _text)
{
  Machine::Registers& regs = _guest.Regs();
  const FormattedQuantity formatted = FormatQuantity(_guest.State(), regs.ax, _text);
  regs.ax = WithLow(formatted.units, static_cast<std::uint8_t>(Low(formatted.units) + '0'));
  regs.di = formatted.blanked.firstKept;
  regs.cx = formatted.blanked.triesLeft;
  regs.si = _text;
}

// What both trade screens start with: the frame from the descriptor at DS:_frame and its title, the two lines of help from
// DS:_help, the header in its attribute, the rows' attribute, and the prices; cargoRowPointer on the first product held.
// _backward is the direction flag, which DrawDockedFrame goes by.
void DrawTradeScreenHeader(GameState& _state, Hardware& _hardware, std::uint16_t _frame, std::uint16_t _help, bool _backward)
{
  PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, _frame, _backward), TITLE_CELL);
  const PrintedText help = PrintTextModeString(_state, _help, LINE_3);
  PrintTextModeString(_state, Offset(help.end, 1), LINE_4); // INC SI: the second line, after the first's NUL
  _state.Set(DS.textAttribute, HEADER_ATTRIBUTE);
  PrintTextModeString(_state, TRADE_HEADER_TEXT, LINE_5);
  _state.Set(DS.textAttribute, ROW_ATTRIBUTE);
  ComputeMarketPrices(_state);
  _state.Set(DS.cargoRowPointer, DS.cargoHold.offset);
}

// DrawTradeScreenHeader, and the registers the first row starts from: SI on the first product's name, DI on its row, BX on
// its prices, and CX the count of rows.
void DrawTradeScreenHeaderOnRegisters(Guest& _guest, std::uint16_t _frame, std::uint16_t _help)
{
  Machine::Registers& regs = _guest.Regs();
  DrawTradeScreenHeader(_guest.State(), _guest.Devices(), _frame, _help, _guest.Flag(Machine::FLAG_DIRECTION));
  regs.si = DS.productNames.offset;
  regs.di = LINE_6;
  regs.bx = DS.screenPrices.offset;
  regs.cx = TRADE_ROWS;
}

// A trade row's product name and price: the price word at BX+_price. Pushed: the row's CX, SI, DI and BX.
void PrintNameAndPrice(Guest& _guest, std::uint16_t _price)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.bx = _guest.Pop();
  regs.ax = _guest.Word(Offset(regs.bx, _price));
  _guest.Push(regs.bx);
  _guest.Push(regs.di);
  _guest.Call(FORMAT_TENTHS);
  regs.si = DS.priceText.offset;
  regs.di = Offset(_guest.Pop(), 4);
  _guest.Call(PRINT_TEXT_MODE_STRING);
}

// After a row's quantity: the next price, then the unit after the name.
void PrintUnit(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = Offset(_guest.Pop(), 4);
  regs.ax = _guest.Pop();
  regs.si = _guest.Pop();
  _guest.Push(regs.si);
  _guest.Push(regs.ax);
  regs.si = Offset(regs.si, UNIT_AFTER_NAME);
  _guest.Call(PRINT_TEXT_MODE_STRING);
}

// The end of a trade row: the next row's text place and product, and the count.
void EndTradeRow(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = Offset(_guest.Pop(), TEXT_ROW_BYTES);
  regs.si = Offset(_guest.Pop(), PRODUCT_NAME_BYTES);
  regs.cx = _guest.Pop();
}

// The title: the planet and the turning ship, F9 and F10 step its type, any other key ends it with the
// credits and a new game. TitleFrameLoop (CS:7DF1) is one frame.
void RunTitle(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(CLEAR_ALL_OBJECTS);
  _guest.Call(START_MUSIC);
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = Guest::VIDEO_SEGMENT;
  _guest.Set(DS.laserMountsFitted, 0);
  _guest.Set(DS.playerPitchAngle, 0);
  _guest.Set(DS.playerYawAngle, 0);
  _guest.Set(DS.playerRollAngle, 0);
  _guest.Set(DS.viewAngle, 0);
  _guest.Set(DS.anyKeyLatch, 0);
  _guest.Set(DS.screenLayout, TITLE_LAYOUT);
  _guest.Call(SHOW_COCKPIT_SCREEN);
  _guest.Call(CLEAR_MESSAGE_LINE);
  _guest.Set(DS.textPaperPattern, 0);
  regs.bx = WHITE_MASK;
  regs.si = DS.eliteTitleText.offset;
  regs.di = TITLE_TEXT_CELL;
  _guest.Call(DRAW_SCREEN_STRING);
  regs.di = DS.stationSlot.offset;
  _guest.Set(DS.shipSlotCount, TITLE_SHIP_SLOTS);
  regs.ax = 0;
  _guest.SetWord(Offset(regs.di, SLOT_YAW), regs.ax);
  _guest.SetWord(Offset(regs.di, SLOT_ROLL), regs.ax);
  _guest.SetWord(Offset(regs.di, SLOT_X), regs.ax);
  _guest.SetWord(Offset(regs.di, SLOT_Y), regs.ax);
  _guest.SetWord(Offset(regs.di, SLOT_X_HIGH), regs.ax);
  _guest.SetByte(Offset(regs.di, SLOT_X_HIGH + 2), Low(regs.ax));
  for (;;)
  {
    regs.bx = TITLE_PLANET_RADIUS;
    regs.dx = TITLE_PLANET_X;
    regs.cx = TITLE_PLANET_Y;
    _guest.Set(DS.drawColor, TITLE_PLANET_COLOR);
    _guest.Call(DRAW_TITLE_PLANET);
    regs.di = DS.stationSlot.offset;
    _guest.SetByte(Offset(regs.di, SLOT_FLAGS), TITLE_SHIP_FLAGS);
    regs.bx = static_cast<std::uint8_t>(_guest.Get(DS.titleShipType) << 1);
    regs.ax = _guest.Word(Offset(DS.titleShipDistances.offset, regs.bx));
    SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) + 1));
    _guest.SetByte(regs.di, Low(regs.bx));
    _guest.SetWord(Offset(regs.di, SLOT_Z), regs.ax); // at the type's own distance
    AddToWord(_guest.State(), Offset(regs.di, SLOT_ROLL), TITLE_ROLL_STEP);
    AddToWord(_guest.State(), Offset(regs.di, SLOT_YAW), TITLE_YAW_STEP);
    AddToWord(_guest.State(), Offset(regs.di, SLOT_PITCH), TITLE_PITCH_STEP);
    regs.si = DS.pressAnyKeyText.offset;
    regs.di = PRESS_ANY_KEY_PLACE;
    _guest.Set(DS.textPaperPattern, 0);
    regs.bx = PRESS_ANY_KEY_MASK;
    _guest.Call(DRAW_VIEW_STRING);
    _guest.Call(TRANSFORM_AND_DRAW_OBJECTS);
    _guest.Call(FINISH_SPACE_VIEW_FRAME);
    _guest.Call(GET_KEY);
    if (!_guest.Flag(Machine::FLAG_ZERO))
    {
      const std::uint8_t key = High(regs.ax);
      if (key == SCAN_F9)
      {
        _guest.Set(DS.titleShipType, static_cast<std::uint8_t>(_guest.Get(DS.titleShipType) - 1));
        if (_guest.Get(DS.titleShipType) < TITLE_FIRST_SHIP_TYPE)
        {
          _guest.Set(DS.titleShipType, TITLE_LAST_SHIP_TYPE);
        }
      }
      else if (key == SCAN_F10)
      {
        _guest.Set(DS.titleShipType, static_cast<std::uint8_t>(_guest.Get(DS.titleShipType) + 1));
        if (_guest.Get(DS.titleShipType) == TITLE_LAST_SHIP_TYPE + 1)
        {
          _guest.Set(DS.titleShipType, TITLE_FIRST_SHIP_TYPE);
        }
      }
      else
      {
        break;
      }
    }
    _guest.JumpBack(TITLE_FRAME_LOOP);
  }
  _guest.Call(STOP_ALL_SOUND);
  _guest.Call(SHOW_CREDITS);
  _guest.Call(START_NEW_GAME);
  _guest.Set(DS.titleShown, 1);
}

// The docked screen for each F-key in DockedKeyDispatch (CS:0B40).
struct DockedScreen
{
  std::uint8_t key;
  std::uint16_t entry;
};

constexpr std::array<DockedScreen, 9> DOCKED_SCREENS = {{
  {SCAN_F2, SHOW_SELL_CARGO_SCREEN},
  {SCAN_F3, SHOW_BUY_CARGO_SCREEN},
  {SCAN_F4, SHOW_EQUIP_SHIP_SCREEN},
  {SCAN_F5, SHOW_GALACTIC_CHART},
  {SCAN_F6, SHOW_SHORT_RANGE_CHART},
  {SCAN_F7, SHOW_SYSTEM_DATA_SCREEN},
  {SCAN_F8, SHOW_MARKET_PRICES_SCREEN},
  {SCAN_F9, SHOW_COMMANDER_STATUS_SCREEN},
  {SCAN_F10, SHOW_INVENTORY_SCREEN},
}};

// DockedKeyDispatch (CS:0B40), from the arm that shows _screen: each screen returns the key that closed
// it, which picks the next screen, until F1 leaves. Esc is the disc menu; any other key waits for one.
void DispatchDockedKeys(Guest& _guest, std::uint16_t _screen)
{
  Machine::Registers& regs = _guest.Regs();
  for (std::uint16_t screen = _screen;;)
  {
    if (screen == SHOW_COMMANDER_STATUS_SCREEN)
    {
      _guest.Call(RESET_KEYBOARD);
    }
    _guest.Call(screen);
    _guest.JumpBack(DOCKED_KEY_TEST);
    for (;;)
    {
      const std::uint8_t key = High(regs.ax);
      if (key == SCAN_F1)
      {
        return;
      }
      const auto found = std::ranges::find(DOCKED_SCREENS, key, &DockedScreen::key);
      if (found != DOCKED_SCREENS.end())
      {
        screen = found->entry;
        break;
      }
      SetHigh(regs.ax, static_cast<std::uint8_t>(key - 1));
      if (High(regs.ax) == 0)
      {
        screen = SHOW_DISC_CONTROL_SCREEN;
        break;
      }
      _guest.JumpBack(DOCKED_KEY_DISPATCH);
      WaitForKey(_guest, DOCKED_KEY_DISPATCH);
    }
  }
}

} // namespace

PrintedText PrintCreditsOnMessageLine(GameState& _state)
{
  SwapTextAttributeNibbles(_state);
  const PrintedText printed = PrintTextModeString(_state, DS.creditBalanceText.offset, CREDITS_ON_MESSAGE_LINE);
  SwapTextAttributeNibbles(_state);
  return printed;
}

void FormatFuelLightYears(GameState& _state)
{
  // MUL AH by 10, then DIV BL by 36, which cannot overflow: at most 2550/36.
  const auto tenths = static_cast<std::uint16_t>(_state.Get(DS.fuel) * FUEL_TENTHS_PER_UNIT);
  FormatDecimal5(_state, static_cast<std::uint8_t>(tenths / FUEL_UNITS_PER_TENTH), FUEL_DIGITS);
  // The units and the tenths, the last two of the five digits, into the text either side of its point.
  _state.Set(DS.data83F7, _state.Get(DS.data840A));
  _state.Set(DS.data83F9, _state.Get(DS.data840B));
}

std::uint16_t DrawDockedFrame(GameState& _state, Hardware& _hardware, std::uint16_t _descriptor, bool _backward)
{
  if ((_state.Get(DS.screenLayout) & TEXT_LAYOUT) == 0)
  {
    _state.Set(DS.screenLayout, TEXT_LAYOUT);
    SetTextMode(_hardware);
  }
  // The screen's attribute, and its background as the border.
  const auto attribute = static_cast<std::uint8_t>(_state.Byte(_descriptor) & ATTRIBUTE_MASK);
  _state.Set(DS.textAttribute, attribute);
  _hardware.SetColorSelect(static_cast<std::uint8_t>(attribute >> 4));
  ClearTextScreen(_state, _backward);

  const std::uint8_t frame = _state.Byte(Offset(_descriptor, 1));
  for (const std::uint16_t row : {FRAME_TOP_ROW, FRAME_TITLE_ROW, FRAME_BOTTOM_ROW})
  {
    DrawFrameRow(_state, GameState::VIDEO_SEGMENT, row, Join(frame, FRAME_ROW_CHARACTER), _backward);
  }
  DrawFrameSides(_state, GameState::VIDEO_SEGMENT, FRAME_SIDES, FRAME_SIDE_ROWS, frame);
  for (std::uint16_t corner = 0; corner < FRAME_CORNERS; ++corner)
  {
    const auto place = static_cast<std::uint16_t>(DS.frameCorners.offset + corner * FRAME_CORNER_BYTES);
    _state.SetVideoWord(_state.Word(place), Join(frame, _state.Byte(Offset(place, 2))));
  }
  return Offset(_descriptor, 2);
}

std::uint16_t DrawFrameSides(GameState& _state, std::uint16_t _segment, std::uint16_t _cell, std::uint16_t _rows, std::uint8_t _attribute)
{
  const std::uint16_t side = Join(_attribute, FRAME_SIDE_CHARACTER);
  std::uint16_t cell = _cell;
  for (std::uint32_t row = 0; row < LoopCount(_rows); ++row)
  {
    _state.SetFarWord(_segment, cell, side);
    _state.SetFarWord(_segment, Offset(cell, FRAME_RIGHT_SIDE), side);
    cell = Offset(cell, TEXT_ROW_BYTES);
  }
  return cell;
}

std::uint16_t DrawFrameRow(GameState& _state, std::uint16_t _segment, std::uint16_t _cell, std::uint16_t _value, bool _backwards)
{
  const auto step = static_cast<std::uint16_t>(_backwards ? 0xFFFE : 2);
  std::uint16_t cell = _cell;
  for (std::uint16_t left = FRAME_ROW_CELLS; left != 0; --left)
  {
    _state.SetFarWord(_segment, cell, _value);
    cell = Offset(cell, step);
  }
  return cell;
}

std::uint8_t AlAfterKey(std::uint8_t _al, const KeyPress& _key) noexcept
{
  // ROL AX,1 / SHR AH,1 when it takes a code: AL shifted left, Shift in bit 0.
  return _key.taken ? static_cast<std::uint8_t>((_al << 1) | (_key.shift ? 1 : 0)) : _al;
}

ScreenKey WaitForScreenExitKey(GameState& _state, Hardware& _hardware, std::uint8_t _ownKey, std::uint8_t _al, std::uint16_t _countIfNone)
{
  // GetKey until Esc or another screen's F-key, every other turn ending at the jump back to 60B4. The turns carry nothing, as
  // WaitForKeyPress's do: GetKey writes AH before it reads it, and reads AL only when it takes a code, which changes keyBuffer.
  std::uint8_t al = _al;
  for (;;)
  {
    const KeyPress key = GetKey(_state, _hardware);
    al = AlAfterKey(al, key);
    const std::uint8_t scanCode = key.scanCode;
    if (scanCode != 0 && scanCode != _ownKey && (scanCode == SCAN_ESCAPE || (scanCode >= SCAN_F1 && scanCode <= SCAN_F10)))
    {
      SelectSystemAtCursor(_state, _countIfNone);
      return ScreenKey{scanCode, al};
    }
    _hardware.LoopTurn(WAIT_FOR_SCREEN_EXIT_KEY, {});
  }
}

void AwardArchangelTitle(GameState& _state)
{
  for (std::uint16_t letter = 0; letter < RANK_LETTERS; ++letter)
  {
    _state.SetByte(DS.commanderRankText.At(letter), _state.Byte(DS.archangelTitle.At(letter)));
  }
}

void ShowSellCargoScreen(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  DrawTradeScreenHeaderOnRegisters(_guest, DS.sellCargoFrame.offset, SELL_CARGO_HELP_TEXT);
  for (;;)
  {
    for (const std::uint16_t value : {regs.cx, regs.si, regs.di, regs.bx})
    {
      _guest.Push(value);
    }
    PrintNameAndPrice(_guest, 2); // the sell price
    regs.bx = _guest.Get(DS.cargoRowPointer);
    SetLow(regs.ax, _guest.Byte(regs.bx)); // the units held
    regs.bx = Offset(regs.bx, 2);
    _guest.Set(DS.cargoRowPointer, regs.bx);
    regs.si = NO_QUANTITY_TEXT;
    regs.ax = Low(regs.ax);
    if (regs.ax != 0)
    {
      _guest.Push(regs.di);
      FormatQuantityOnRegisters(_guest, DS.quantityText.offset);
      regs.di = Offset(_guest.Pop(), 2);
      _guest.Call(PRINT_TEXT_MODE_STRING);
      PrintUnit(_guest);
    }
    else
    {
      _guest.Call(PRINT_TEXT_MODE_STRING);
      regs.bx = Offset(_guest.Pop(), 4);
    }
    EndTradeRow(_guest);
    if (--regs.cx == 0)
    {
      break;
    }
    _guest.JumpBack(SELL_CARGO_ROW);
  }
  _guest.Set(DS.tradeScreenIsBuy, 0);
  _guest.Call(RUN_CARGO_TRADE_MENU);
}

void ShowBuyCargoScreen(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = _guest.Get(DS.randomState0);
  _guest.SetWord(DS.marketRandomState.offset, regs.ax);
  regs.ax = _guest.Get(DS.randomState1);
  _guest.Set(DS.data8C20, regs.ax);
  regs.ax = _guest.Get(DS.randomState2);
  _guest.Set(DS.data8C22, regs.ax);
  DrawTradeScreenHeaderOnRegisters(_guest, DS.buyCargoFrame.offset, BUY_CARGO_HELP_TEXT);
  for (;;)
  {
    for (const std::uint16_t value : {regs.cx, regs.si, regs.di, regs.bx})
    {
      _guest.Push(value);
    }
    PrintNameAndPrice(_guest, 0); // the buy price
    regs.si = NO_QUANTITY_TEXT;
    bool onSale = true;
    if (_guest.Get(DS.marketQuantitiesSet) == 0)
    {
      // The quantity on sale, the first time after an arrival: ((r & 31) - 7) xor (r >> 8 & 3).
      _guest.Call(NEXT_MARKET_RANDOM);
      const auto random = static_cast<std::uint8_t>(Low(regs.ax) & QUANTITY_RANDOM_MASK);
      SetLow(regs.ax, static_cast<std::uint8_t>(random - QUANTITY_RANDOM_BIAS));
      onSale = random >= QUANTITY_RANDOM_BIAS;
      if (onSale)
      {
        SetHigh(regs.ax, static_cast<std::uint8_t>(High(regs.ax) & QUANTITY_RANDOM_HIGH_MASK));
        regs.ax = static_cast<std::uint8_t>(Low(regs.ax) ^ High(regs.ax));
      }
    }
    else
    {
      regs.bx = _guest.Get(DS.cargoRowPointer);
      regs.ax = _guest.Byte(Offset(regs.bx, 1));
      onSale = regs.ax != 0;
    }
    if (onSale)
    {
      _guest.Push(regs.ax);
      _guest.Push(regs.di);
      FormatQuantityOnRegisters(_guest, DS.quantityText.offset);
      regs.di = Offset(_guest.Pop(), 2);
      _guest.Call(PRINT_TEXT_MODE_STRING);
      regs.bx = _guest.Get(DS.cargoRowPointer);
      regs.ax = _guest.Pop();
      _guest.SetByte(Offset(regs.bx, 1), Low(regs.ax));
      regs.bx = Offset(regs.bx, 2);
      _guest.Set(DS.cargoRowPointer, regs.bx);
      PrintUnit(_guest);
    }
    else
    {
      regs.si = NO_QUANTITY_TEXT;
      _guest.Call(PRINT_TEXT_MODE_STRING);
      regs.bx = _guest.Get(DS.cargoRowPointer);
      _guest.SetByte(Offset(regs.bx, 1), 0);
      regs.bx = Offset(regs.bx, 2);
      _guest.Set(DS.cargoRowPointer, regs.bx);
      regs.bx = Offset(_guest.Pop(), 4);
      _guest.JumpBack(BUY_CARGO_ROW_END);
    }
    EndTradeRow(_guest);
    if (--regs.cx == 0)
    {
      break;
    }
    _guest.JumpBack(BUY_CARGO_ROW);
  }
  _guest.Set(DS.tradeScreenIsBuy, 1);
  _guest.Set(DS.marketQuantitiesSet, 1);
  _guest.Call(RUN_CARGO_TRADE_MENU);
}

ScreenKey ShowCommanderStatusScreen(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _countIfNone)
{
  if (_state.Get(DS.inFlight) != 1 && _state.Get(DS.missionNumber) != 0)
  {
    if (_state.Get(DS.missionStage) == 0)
    {
      ShowMissionBriefing(_state, _hardware, _backward);
    }
    else if (_state.Get(DS.missionStage) == STAGE_BRIEFED && _state.Get(DS.playerDocked) == 1)
    {
      ShowMissionDebriefing(_state, _hardware, _backward);
    }
  }
  const PrintedText title =
    PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, DS.commanderTitle.offset, _backward), TITLE_CELL);
  PrintTextModeString(_state, DS.defaultCommanderName.offset, title.nextCell);
  _state.Set(DS.textAttribute, STATUS_ATTRIBUTE);
  const bool inWitchSpace = _state.Get(DS.witchspaceCountdown) != 0;
  PrintedText printed = PrintTextModeString(_state, SYSTEM_LABEL_TEXT, LINE_3);
  PrintTextModeString(_state, inWitchSpace ? WITCH_SPACE_TEXT : DS.currentSystemName.offset, printed.nextCell);
  printed = PrintTextModeString(_state, HYPERSYSTEM_LABEL_TEXT, LINE_4);
  PrintTextModeString(_state, inWitchSpace ? WITCH_SPACE_TEXT : DS.selectedSystemName.offset, printed.nextCell);
  FormatFuelLightYears(_state);
  PrintTextModeString(_state, FUEL_TEXT, LINE_5);
  printed = PrintTextModeString(_state, CASH_LABEL_TEXT, LINE_6);
  PrintTextModeString(_state, SkipCashSpaces(_state, _hardware, STATUS_CASH_SPACE), printed.nextCell);
  printed = PrintTextModeString(_state, LEGAL_STATUS_LABEL_TEXT, LINE_7);
  const std::uint8_t legal = _state.Get(DS.legalStatus);
  const std::uint16_t standing = legal < LEGAL_STATUS_OFFENDER ? 0 : legal < LEGAL_STATUS_FUGITIVE ? 1 : 2;
  PrintTextModeString(_state, _state.Word(DS.legalStatusNames.At(standing)), printed.nextCell);
  printed = PrintTextModeString(_state, RATING_LABEL_TEXT, LINE_8);
  // The rating: the first threshold above the kill count, each turn of the search carrying the threshold's place and the rating.
  const std::uint8_t kills = _state.Get(DS.killCount);
  std::uint16_t threshold = static_cast<std::uint16_t>(DS.ratingThresholds.offset - 1);
  std::uint16_t rating = 0xFFFF;
  for (;;)
  {
    threshold = Offset(threshold, 1);
    rating = Offset(rating, 1);
    if (kills < _state.Byte(threshold))
    {
      break;
    }
    _hardware.LoopTurn(STATUS_RATING, {threshold, rating});
  }
  PrintTextModeString(_state, _state.Word(DS.ratingNames.At(rating)), printed.nextCell);
  _state.Set(DS.textAttribute, EQUIPMENT_HEADING_ATTRIBUTE);
  PrintTextModeString(_state, EQUIPMENT_HEADING_TEXT, LINE_9);
  _state.Set(DS.textAttribute, STATUS_ATTRIBUTE);
  // AL keeps the missiles' digit until a row prints, whose NUL then leaves 0 there.
  auto al = static_cast<std::uint8_t>(_state.Get(DS.missileCount) + DIGIT_ZERO);
  _state.Set(DS.data84F5, al);

  // One row for each item fitted, and after a laser the mounts it is fitted to. Each turn carries the count, the row's place,
  // the item's count and its name's pointer.
  std::uint16_t row = LINE_10;
  std::uint16_t fitted = DS.missileCount.offset;
  std::uint16_t name = DS.equipmentNames.offset;
  _state.Set(DS.menuSelectedRow, FIRST_EQUIPMENT_MENU_ROW);
  for (std::uint16_t left = EQUIPMENT_ROWS;;)
  {
    if (_state.Byte(fitted) != 0)
    {
      printed = PrintTextModeString(_state, _state.Word(name), row);
      al = 0;
      if (SelectLaserType(_state))
      {
        printed = PrintTextModeString(_state, MOUNTS_OPEN_TEXT, printed.nextCell);
        // Each mount with this type of laser, from laserMountsFitted in DL and laserMountTypes in DH, which each turn carries
        // with the mount name's pointer, the count and where the next name prints.
        std::uint8_t mounts = _state.Get(DS.laserMountsFitted);
        std::uint8_t types = _state.Get(DS.laserMountTypes);
        std::uint16_t mountName = DS.laserMountNames.offset;
        for (std::uint16_t mount = LASER_MOUNTS;;)
        {
          const bool on = (mounts & 1) != 0;
          mounts = static_cast<std::uint8_t>(mounts >> 1);
          if (on && (types & LASER_MOUNT_TYPE_MASK) == _state.Get(DS.selectedLaserType))
          {
            printed = PrintTextModeString(_state, _state.Word(mountName), printed.nextCell);
          }
          mountName = Offset(mountName, 2);
          types = static_cast<std::uint8_t>(types >> 2);
          if (--mount == 0)
          {
            break;
          }
          _hardware.LoopTurn(STATUS_LASER_MOUNT, {Join(types, mounts), mountName, mount, printed.nextCell});
        }
        PrintTextModeString(_state, MOUNTS_CLOSE_TEXT, static_cast<std::uint16_t>(printed.nextCell - 2));
      }
      row = Offset(row, TEXT_ROW_BYTES);
    }
    fitted = Offset(fitted, 1);
    name = Offset(name, 2);
    _state.Set(DS.menuSelectedRow, static_cast<std::uint8_t>(_state.Get(DS.menuSelectedRow) + 1));
    if (--left == 0)
    {
      break;
    }
    _hardware.LoopTurn(STATUS_EQUIPMENT_ROW, {left, row, fitted, name});
  }
  return WaitForScreenExitKey(_state, _hardware, SCAN_F9, al, _countIfNone);
}

ScreenKey ShowInventoryScreen(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _countIfNone)
{
  PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, DS.inventoryFrame.offset, _backward), TITLE_CELL);
  // The fuel text without the padding the status screen has: 'Fuel: ', cut by a NUL that becomes a space again, then
  // 'n.n Light Years'.
  FormatFuelLightYears(_state);
  _state.SetByte(Offset(FUEL_TEXT, FUEL_LABEL_BYTES), NUL);
  const PrintedText label = PrintTextModeString(_state, FUEL_TEXT, LINE_3);
  _state.SetByte(label.end, SPACE);
  PrintTextModeString(_state, Offset(label.end, FUEL_DIGITS_AFTER_LABEL), label.nextCell);
  const PrintedText cash = PrintTextModeString(_state, CASH_LABEL_TEXT, LINE_4);
  const auto cashCell = static_cast<std::uint16_t>(cash.nextCell - INVENTORY_CASH_BACK);
  PrintTextModeString(_state, SkipCashSpaces(_state, _hardware, INVENTORY_CASH_SPACE), cashCell);

  // Every product held, and the refugees: name, quantity, unit, a row each. Each turn carries the count, the name, the amount's
  // place and the row's place, which an empty row does not move on.
  std::uint16_t row = LINE_6;
  std::uint16_t name = DS.productNames.offset;
  std::uint16_t held = DS.cargoHold.offset;
  for (std::uint16_t left = INVENTORY_ROWS;;)
  {
    const std::uint8_t amount = _state.Byte(held);
    if (amount == 0)
    {
      _hardware.LoopTurn(INVENTORY_ROW_END, {left, name, held, row});
    }
    else
    {
      // The name cut at its twelfth character by a NUL, which becomes a space again before the unit after it.
      _state.SetByte(Offset(name, PRODUCT_NAME_END), NUL);
      const PrintedText product = PrintTextModeString(_state, name, row);
      _state.SetByte(product.end, SPACE);
      FormatQuantity(_state, amount, INVENTORY_QUANTITY_TEXT);
      const PrintedText quantity = PrintTextModeString(_state, INVENTORY_QUANTITY_TEXT, Offset(product.nextCell, 4));
      PrintTextModeString(_state, product.end, quantity.nextCell);
      row = Offset(row, TEXT_ROW_BYTES);
    }
    held = Offset(held, 2);
    name = Offset(name, PRODUCT_NAME_BYTES);
    if (--left == 0)
    {
      break;
    }
    _hardware.LoopTurn(INVENTORY_ROW, {left, name, held, row});
  }
  // The last row leaves AL 0: an empty one its amount, a printed one the NUL its last print stops at.
  return WaitForScreenExitKey(_state, _hardware, SCAN_F10, 0, _countIfNone);
}

void ShowMissionBriefing(GameState& _state, Hardware& _hardware, bool _backward)
{
  _state.Set(DS.missionStage, STAGE_BRIEFED);
  PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, DS.emergencyFrame.offset, _backward), TITLE_CELL);
  const std::uint8_t mission = _state.Get(DS.missionNumber);
  if (mission == MISSION_SUPERNOVA)
  {
    PrintTextLines(_state, DS.supernovaBriefingText.offset, LINE_4, SUPERNOVA_BRIEFING_LINES);
    // Y takes the refugees, N refuses; every other key goes back to the GetKey at 6E23, as an empty one does.
    std::uint8_t answer = 0;
    for (;;)
    {
      answer = WaitForKey(_state, _hardware, SUPERNOVA_ANSWER).scanCode;
      if (answer == SCAN_Y || answer == SCAN_N)
      {
        break;
      }
      _hardware.LoopTurn(SUPERNOVA_ANSWER, {});
    }
    std::uint16_t reply = SUPERNOVA_REFUSED_TEXT;
    if (answer == SCAN_Y)
    {
      // The hold emptied for the refugees: MOV [DI],CH, which is 0, at each of the 17 amounts, the LOOP's turns carrying DI
      // and CX.
      std::uint16_t held = DS.cargoHold.offset;
      for (std::uint16_t left = TRADE_ROWS;;)
      {
        _state.SetByte(held, 0);
        held = Offset(held, 2);
        if (--left == 0)
        {
          break;
        }
        _hardware.LoopTurn(EVACUATE_CARGO, {held, left});
      }
      _state.Set(DS.cargoUsedTonnes, REFUGEE_TONNES);
      if (_state.Get(DS.largeCargoBayFitted) == 1)
      {
        _state.Set(DS.cargoUsedTonnes, REFUGEE_TONNES_LARGE_BAY);
      }
      _state.Set(DS.refugeesTonnes, _state.Get(DS.cargoUsedTonnes));
      reply = SUPERNOVA_ACCEPTED_TEXT;
    }
    PrintTextLines(_state, reply, LINE_10, SUPERNOVA_ANSWER_LINES);
    (void)WaitForKeyPress(_state, _hardware);
    _state.Set(DS.supernovaFrames, SUPERNOVA_DELAY_FRAMES);
    _state.Set(DS.jumpedSinceBriefing, 0);
    return;
  }
  if (mission == MISSION_MASK_SHIP)
  {
    PrintTextLines(_state, DS.maskBriefingText.offset, LINE_4, MASK_BRIEFING_LINES);
    (void)WaitForKeyPress(_state, _hardware);
    _state.Set(DS.maskMissionShipsLeft, MASK_SHIPS);
    _state.Set(DS.maskSystemJumps, MASK_SYSTEM_JUMPS);
    return;
  }
  PrintTextLines(_state, DS.invasionBriefingText.offset, LINE_4, INVASION_BRIEFING_LINES);
  (void)WaitForKeyPress(_state, _hardware);
  _state.Set(DS.thargoidInvasionActive, 1);
  _state.Set(DS.jumpedSinceBriefing, 0);
}

void ShowMissionDebriefing(GameState& _state, Hardware& _hardware, bool _backward)
{
  // Not yet: the mask ship is still about, or the supernova's refugees have not been taken away.
  if (_state.Get(DS.missionNumber) == MISSION_MASK_SHIP
        ? _state.Get(DS.maskShipDestroyed) != 1
        : _state.Get(DS.missionNumber) == MISSION_SUPERNOVA && _state.Get(DS.jumpedSinceBriefing) != 1)
  {
    return;
  }
  PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, DS.taskCompleteFrame.offset, _backward), TITLE_CELL);
  _state.Set(DS.missionStage, STAGE_DEBRIEFED);
  const std::uint8_t mission = _state.Get(DS.missionNumber);
  if (mission == MISSION_SUPERNOVA)
  {
    const bool fullHold = _state.Get(DS.refugeesTonnes) == REFUGEE_TONNES;
    _state.Set(DS.refugeeRewardDigit, fullHold ? REWARD_DIGIT_FULL_HOLD : REWARD_DIGIT_OTHERWISE);
    _state.Set(DS.refugeesTonnes, 0);
    _state.Set(DS.cargoUsedTonnes, 0);
    AddCredits(_state, fullHold ? REWARD_FULL_HOLD_TENTHS : REWARD_OTHERWISE_TENTHS);
    PrintTextLines(_state, SUPERNOVA_DEBRIEFING_TEXT, LINE_4, SUPERNOVA_DEBRIEFING_LINES);
    (void)WaitForKeyPress(_state, _hardware);
    _state.Set(DS.missionNumber, 0);
    _state.Set(DS.missionStage, 0);
    return;
  }
  if (mission == MISSION_MASK_SHIP)
  {
    std::uint16_t text = MASK_DESTROYED_TEXT;
    std::uint16_t lines = MASK_DESTROYED_LINES;
    if (_state.Get(DS.maskingDeviceRecovered) == 1)
    {
      _state.Set(DS.maskingDeviceFitted, 1);
      text = MASK_RECOVERED_TEXT;
      lines = MASK_RECOVERED_LINES;
      _hardware.LoopTurn(MASK_DEBRIEFING_TEXT, {}); // the JMP back to the print the other two run on into
    }
    else if (_state.Get(DS.fledMaskShip) == 1)
    {
      text = MASK_FLED_TEXT;
      lines = MASK_FLED_LINES;
    }
    PrintTextLines(_state, text, LINE_4, lines);
    (void)WaitForKeyPress(_state, _hardware);
    _state.Set(DS.maskMissionShipsLeft, 0);
    _state.Set(DS.maskShipDestroyed, 0);
    _state.Set(DS.missionNumber, 0);
    _state.Set(DS.maskingDeviceRecovered, 0);
    _state.Set(DS.maskSystemJumps, 0);
    _state.Set(DS.fledMaskShip, 0);
    _state.Set(DS.missionStage, 0);
    return;
  }
  PrintTextLines(_state, DS.invasionDebriefText.offset, LINE_4, INVASION_DEBRIEFING_LINES);
  (void)WaitForKeyPress(_state, _hardware);
  AwardArchangelTitle(_state);
  _state.Set(DS.antiEcmEmulatorFitted, 1);
  _state.Set(DS.missionNumber, 0);
  _state.Set(DS.missionStage, 0);
  _state.Set(DS.invadedStationDestroyed, 0);
  _state.Set(DS.thargoidInvasionActive, 0);
  _state.Set(DS.missionStage, 0);
}

void RunTitleAndDocked(Guest& _guest)
{
  if (_guest.Get(DS.titleShown) != 1)
  {
    RunTitle(_guest);
  }
  // Into DockedKeyDispatch: back from a disk request, at the disc menu; otherwise at the status screen.
  if (_guest.Get(DS.resumeAtDiskMenu) == 1)
  {
    _guest.JumpBack(DOCKED_DISK_MENU_ENTRY);
    DispatchDockedKeys(_guest, SHOW_DISC_CONTROL_SCREEN);
    return;
  }
  _guest.JumpBack(DOCKED_STATUS_ENTRY);
  DispatchDockedKeys(_guest, SHOW_COMMANDER_STATUS_SCREEN);
}

// ── Their entries ──

namespace
{

using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DX;
using Machine::REGISTER_ES;
using Machine::REGISTER_SI;

constexpr Machine::NativeContract CLOBBERS_AX_BX_DI{REGISTER_AX | REGISTER_BX | REGISTER_DI, 0};
constexpr Machine::NativeContract CLOBBERS_AX_CX_SI_DI{REGISTER_AX | REGISTER_CX | REGISTER_SI | REGISTER_DI, 0};
// What a docked screen leaves: everything but DS (it waits as a rule, so it is never compared).
constexpr Machine::NativeContract CLOBBERS_ALL{
  REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP | REGISTER_ES, 0};
// What a docked screen that returns its closing key leaves: everything but AX, the key in AH, and DS.
constexpr Machine::NativeContract SHOWS_SCREEN{
  REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP | REGISTER_ES, 0};

} // namespace

void ShowCommanderStatusScreenEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // BP, the count SelectSystemAtCursor makes the index from when no system is on the chart.
  const ScreenKey key = ShowCommanderStatusScreen(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION), regs.bp);
  regs.ax = Join(key.scanCode, key.al);
  _guest.Clobber(SHOWS_SCREEN);
}

void ShowInventoryScreenEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ScreenKey key = ShowInventoryScreen(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION), regs.bp);
  regs.ax = Join(key.scanCode, key.al);
  _guest.Clobber(SHOWS_SCREEN);
}

void ShowMissionBriefingEntry(Guest& _guest)
{
  ShowMissionBriefing(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION));
  _guest.Clobber(CLOBBERS_ALL);
}

void ShowMissionDebriefingEntry(Guest& _guest)
{
  ShowMissionDebriefing(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION));
  _guest.Clobber(CLOBBERS_ALL);
}

void AwardArchangelTitleEntry(Guest& _guest)
{
  AwardArchangelTitle(_guest.State());
  _guest.Clobber(CLOBBERS_AX_CX_SI_DI);
}

void PrintCreditsOnMessageLineEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const PrintedText printed = PrintCreditsOnMessageLine(_guest.State());
  regs.di = printed.nextCell;
  regs.es = Guest::VIDEO_SEGMENT;
  // The original keeps SI and CX round the print. It leaves AH the swapped attribute PrintTextModeString printed in,
  // and AL the attribute swapped back.
  const std::uint8_t attribute = _guest.Get(DS.textAttribute);
  regs.ax = Join(static_cast<std::uint8_t>((attribute >> 4) | (attribute << 4)), attribute);
  _guest.Clobber(PRESERVES_ALL);
}

void FormatFuelLightYearsEntry(Guest& _guest)
{
  FormatFuelLightYears(_guest.State());
  _guest.Regs().si = FUEL_TEXT;
  _guest.Clobber(CLOBBERS_AX_BX_DI);
}

void DrawDockedFrameEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t descriptor = regs.si;
  regs.si = DrawDockedFrame(_guest.State(), _guest.Devices(), descriptor, _guest.Flag(Machine::FLAG_DIRECTION));
  // The contract keeps every register, so the entry leaves what the original does: DX the colour select port it wrote,
  // ES on the text page, and from the corners' LOOP, BX past frameCorners, CX = 0, DI on the last corner, and AX its
  // character in the frame's attribute.
  const auto lastCorner = static_cast<std::uint16_t>(DS.frameCorners.offset + (FRAME_CORNERS - 1) * FRAME_CORNER_BYTES);
  regs.dx = BORDER_COLOR_PORT;
  regs.es = Guest::VIDEO_SEGMENT;
  regs.bx = Offset(lastCorner, FRAME_CORNER_BYTES);
  regs.cx = 0;
  regs.di = _guest.Word(lastCorner);
  regs.ax = Join(_guest.Byte(Offset(descriptor, 1)), _guest.Byte(Offset(lastCorner, 2)));
  _guest.Clobber(PRESERVES_ALL);
}

void DrawFrameSidesEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = DrawFrameSides(_guest.State(), regs.es, regs.di, regs.cx, High(regs.ax));
  // The original puts the side's character in AL, and its LOOP leaves CX at 0.
  SetLow(regs.ax, FRAME_SIDE_CHARACTER);
  regs.cx = 0;
  _guest.Clobber(PRESERVES_ALL);
}

void DrawFrameRowEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = DrawFrameRow(_guest.State(), regs.es, regs.di, regs.ax, _guest.Flag(Machine::FLAG_DIRECTION));
  // Its REP STOSW leaves CX at 0.
  regs.cx = 0;
  _guest.Clobber(PRESERVES_ALL);
}

namespace
{

constexpr Machine::NativeReturn NEAR = Machine::NativeReturn::Near;
constexpr Machine::NativeWait ALWAYS = Machine::NativeWait::Always;

constexpr std::array ENTRIES = {
  NativeEntry{0x49E4, "AwardArchangelTitle", &AwardArchangelTitleEntry, CLOBBERS_AX_CX_SI_DI},
  NativeEntry{0x5A30, "ShowSellCargoScreen", &ShowSellCargoScreen, CLOBBERS_ALL, NEAR, 0, ALWAYS},
  NativeEntry{0x5AE9, "ShowBuyCargoScreen", &ShowBuyCargoScreen, CLOBBERS_ALL, NEAR, 0, ALWAYS},
  NativeEntry{0x5EA9, "ShowCommanderStatusScreen", &ShowCommanderStatusScreenEntry, SHOWS_SCREEN, NEAR, 0, ALWAYS},
  NativeEntry{0x6020, "ShowInventoryScreen", &ShowInventoryScreenEntry, SHOWS_SCREEN, NEAR, 0, ALWAYS},
  NativeEntry{0x658F, "PrintCreditsOnMessageLine", &PrintCreditsOnMessageLineEntry, PRESERVES_ALL},
  NativeEntry{0x6923, "FormatFuelLightYears", &FormatFuelLightYearsEntry, CLOBBERS_AX_BX_DI},
  NativeEntry{0x6DF2, "ShowMissionBriefing", &ShowMissionBriefingEntry, CLOBBERS_ALL, NEAR, 0, ALWAYS},
  NativeEntry{0x6EB7, "ShowMissionDebriefing", &ShowMissionDebriefingEntry, CLOBBERS_ALL, NEAR, 0, ALWAYS},
  NativeEntry{0x7C88, "DrawDockedFrame", &DrawDockedFrameEntry, PRESERVES_ALL},
  NativeEntry{0x7CE9, "DrawFrameSides", &DrawFrameSidesEntry, PRESERVES_ALL},
  NativeEntry{0x7CF8, "DrawFrameRow", &DrawFrameRowEntry, PRESERVES_ALL},
  NativeEntry{0x7D81, "RunTitleAndDocked", &RunTitleAndDocked, CLOBBERS_ALL, NEAR, 0, ALWAYS},
};

} // namespace

std::span<const NativeEntry> DockedEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
