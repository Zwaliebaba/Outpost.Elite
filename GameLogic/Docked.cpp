#include "pch.h"

#include "Docked.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Ships.h"
#include "Text.h"

#include <algorithm>
#include <initializer_list>

namespace Elite
{

namespace
{

constexpr std::uint16_t CLEAR_TEXT_SCREEN = 0x7C12;
constexpr std::uint16_t SET_TEXT_MODE = 0x7D11;

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
constexpr std::uint16_t SELECT_SYSTEM_AT_CURSOR = 0x1199;
constexpr std::uint16_t DRAW_VIEW_STRING = 0x31EC;
constexpr std::uint16_t DRAW_SCREEN_STRING = 0x32D8;
constexpr std::uint16_t FORMAT_DECIMAL5 = 0x3407;
constexpr std::uint16_t BLANK_LEADING_ZEROS = 0x3432;
constexpr std::uint16_t CLEAR_MESSAGE_LINE = 0x3609;
constexpr std::uint16_t TRANSFORM_AND_DRAW_OBJECTS = 0x3D25;
constexpr std::uint16_t START_NEW_GAME = 0x4671;
constexpr std::uint16_t AWARD_ARCHANGEL_TITLE = 0x49E4;
constexpr std::uint16_t CLEAR_ALL_OBJECTS = 0x52B2;
constexpr std::uint16_t SHOW_SELL_CARGO_SCREEN = 0x5A30;
constexpr std::uint16_t SHOW_BUY_CARGO_SCREEN = 0x5AE9;
constexpr std::uint16_t SHOW_EQUIP_SHIP_SCREEN = 0x5BF2;
constexpr std::uint16_t SHOW_SYSTEM_DATA_SCREEN = 0x5CDE;
constexpr std::uint16_t SHOW_MARKET_PRICES_SCREEN = 0x5E2C;
constexpr std::uint16_t SHOW_COMMANDER_STATUS_SCREEN = 0x5EA9;
constexpr std::uint16_t SHOW_INVENTORY_SCREEN = 0x6020;
constexpr std::uint16_t PRINT_TEXT_MODE_STRING = 0x60D2;
constexpr std::uint16_t SELECT_LASER_TYPE = 0x633B;
constexpr std::uint16_t ADD_CREDITS = 0x65EE;
constexpr std::uint16_t SHOW_DISC_CONTROL_SCREEN = 0x660B;
constexpr std::uint16_t FORMAT_FUEL_LIGHT_YEARS = 0x6923;
constexpr std::uint16_t FORMAT_TENTHS = 0x69B3;
constexpr std::uint16_t COMPUTE_MARKET_PRICES = 0x69CE;
constexpr std::uint16_t NEXT_MARKET_RANDOM = 0x6A85;
constexpr std::uint16_t RUN_CARGO_TRADE_MENU = 0x6B1E;
constexpr std::uint16_t PRINT_TEXT_LINES = 0x6DDE;
constexpr std::uint16_t WAIT_FOR_KEY_PRESS = 0x6DEC;
constexpr std::uint16_t SHOW_MISSION_BRIEFING = 0x6DF2;
constexpr std::uint16_t SHOW_MISSION_DEBRIEFING = 0x6EB7;
constexpr std::uint16_t START_MUSIC = 0x7401;
constexpr std::uint16_t STOP_ALL_SOUND = 0x7423;
constexpr std::uint16_t GET_KEY = 0x7616;
constexpr std::uint16_t RESET_KEYBOARD = 0x7668;
constexpr std::uint16_t SHOW_COCKPIT_SCREEN = 0x7BC0;
constexpr std::uint16_t DRAW_DOCKED_FRAME = 0x7C88;
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

// The original's backward jump to CS:_target, where one turn of a loop ends (ADR-008): paced time looks
// at it there (Guest::LoopTurn), with IP where the original's jump leaves it.
void JumpBack(Guest& _guest, std::uint16_t _target)
{
  _guest.Regs().ip = _target;
  _guest.LoopTurn();
}

// ADD WORD PTR [_offset],_value.
void AddToWord(Guest& _guest, std::uint16_t _offset, std::uint16_t _value)
{
  _guest.SetWord(_offset, Offset(_guest.Word(_offset), _value));
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
    JumpBack(_guest, _loop);
  }
}

// AX as a quantity, in the text at _text with up to four leading zeros blanked. Out: SI=_text.
void FormatQuantity(Guest& _guest, std::uint16_t _text)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = _text;
  _guest.Call(FORMAT_DECIMAL5);
  regs.di = _text;
  regs.cx = QUANTITY_DIGITS_BLANKED;
  _guest.Call(BLANK_LEADING_ZEROS);
  regs.si = _text;
}

// What both trade screens start with: the frame, the help text and the header, and the prices. Out: the
// registers for the first row.
void DrawTradeScreenHeader(Guest& _guest, std::uint16_t _frame, std::uint16_t _help)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = _frame;
  _guest.Call(DRAW_DOCKED_FRAME);
  regs.di = TITLE_CELL;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = _help;
  regs.di = LINE_3;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  ++regs.si;
  regs.di = LINE_4;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = TRADE_HEADER_TEXT;
  regs.di = LINE_5;
  _guest.Set(DS.textAttribute, HEADER_ATTRIBUTE);
  _guest.Call(PRINT_TEXT_MODE_STRING);
  _guest.Set(DS.textAttribute, ROW_ATTRIBUTE);
  _guest.Call(COMPUTE_MARKET_PRICES);
  regs.si = DS.productNames.offset;
  regs.di = LINE_6;
  regs.bx = DS.cargoHold.offset;
  _guest.Set(DS.cargoRowPointer, regs.bx);
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
    AddToWord(_guest, Offset(regs.di, SLOT_ROLL), TITLE_ROLL_STEP);
    AddToWord(_guest, Offset(regs.di, SLOT_YAW), TITLE_YAW_STEP);
    AddToWord(_guest, Offset(regs.di, SLOT_PITCH), TITLE_PITCH_STEP);
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
    JumpBack(_guest, TITLE_FRAME_LOOP);
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
    JumpBack(_guest, DOCKED_KEY_TEST);
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
      JumpBack(_guest, DOCKED_KEY_DISPATCH);
      WaitForKey(_guest, DOCKED_KEY_DISPATCH);
    }
  }
}

} // namespace

void PrintCreditsOnMessageLine(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  SwapTextAttributeNibbles(_guest);
  const std::uint16_t cx = regs.cx;
  const std::uint16_t si = regs.si;
  regs.si = DS.creditBalanceText.offset;
  regs.di = CREDITS_ON_MESSAGE_LINE;
  PrintTextModeString(_guest);
  regs.si = si;
  regs.cx = cx;
  SwapTextAttributeNibbles(_guest);
}

void FormatFuelLightYears(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const auto tenths = static_cast<std::uint16_t>(_guest.Get(DS.fuel) * FUEL_TENTHS_PER_UNIT);
  regs.bx = WithLow(regs.bx, FUEL_UNITS_PER_TENTH);
  // DIV BL, which cannot overflow: at most 2550/36.
  regs.ax = static_cast<std::uint8_t>(tenths / FUEL_UNITS_PER_TENTH);
  regs.di = FUEL_DIGITS;
  FormatDecimal5(_guest);
  regs.ax = WithLow(regs.ax, _guest.Get(DS.data840A));
  _guest.Set(DS.data83F7, Low(regs.ax));
  regs.ax = WithLow(regs.ax, _guest.Get(DS.data840B));
  _guest.Set(DS.data83F9, Low(regs.ax));
  regs.si = FUEL_TEXT;
}

void DrawDockedFrame(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if ((_guest.Get(DS.screenLayout) & TEXT_LAYOUT) == 0)
  {
    _guest.Set(DS.screenLayout, TEXT_LAYOUT);
    _guest.Call(SET_TEXT_MODE);
  }
  // The screen's attribute, and its background as the border.
  regs.dx = BORDER_COLOR_PORT;
  const auto attribute = static_cast<std::uint8_t>(_guest.Byte(regs.si) & ATTRIBUTE_MASK);
  ++regs.si;
  _guest.Set(DS.textAttribute, attribute);
  regs.ax = static_cast<std::uint16_t>((attribute << 8) | (attribute >> 4));
  _guest.Out8(regs.dx, Low(regs.ax));
  _guest.Call(CLEAR_TEXT_SCREEN);

  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = Guest::VIDEO_SEGMENT;
  regs.ax = static_cast<std::uint16_t>((_guest.Byte(regs.si) << 8) | FRAME_ROW_CHARACTER);
  ++regs.si;
  for (const std::uint16_t row : {FRAME_TOP_ROW, FRAME_TITLE_ROW, FRAME_BOTTOM_ROW})
  {
    regs.di = row;
    DrawFrameRow(_guest);
  }
  regs.di = FRAME_SIDES;
  regs.cx = FRAME_SIDE_ROWS;
  DrawFrameSides(_guest);
  regs.bx = DS.frameCorners.offset;
  regs.cx = FRAME_CORNERS;
  do
  {
    regs.di = _guest.Word(regs.bx);
    regs.ax = WithLow(regs.ax, _guest.Byte(static_cast<std::uint16_t>(regs.bx + 2)));
    regs.bx = static_cast<std::uint16_t>(regs.bx + FRAME_CORNER_BYTES);
    _guest.SetFarWord(regs.es, regs.di, regs.ax);
    --regs.cx;
  } while (regs.cx != 0);
}

void DrawFrameSides(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = WithLow(regs.ax, FRAME_SIDE_CHARACTER);
  do
  {
    _guest.SetFarWord(regs.es, regs.di, regs.ax);
    _guest.SetFarWord(regs.es, static_cast<std::uint16_t>(regs.di + FRAME_RIGHT_SIDE), regs.ax);
    regs.di = static_cast<std::uint16_t>(regs.di + TEXT_ROW_BYTES);
    --regs.cx;
  } while (regs.cx != 0);
}

void DrawFrameRow(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // REP STOSW, forwards or, with DF set, backwards.
  const auto step = static_cast<std::uint16_t>((regs.flags & Machine::FLAG_DIRECTION) != 0 ? 0xFFFE : 2);
  for (regs.cx = FRAME_ROW_CELLS; regs.cx != 0; --regs.cx)
  {
    _guest.SetFarWord(regs.es, regs.di, regs.ax);
    regs.di = static_cast<std::uint16_t>(regs.di + step);
  }
}

void WaitForScreenExitKey(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  for (;;)
  {
    _guest.Call(GET_KEY);
    if (!_guest.Flag(Machine::FLAG_ZERO))
    {
      const std::uint8_t key = High(regs.ax);
      if (key != Low(regs.dx) && (key == SCAN_ESCAPE || (key >= SCAN_F1 && key <= SCAN_F10)))
      {
        break;
      }
    }
    JumpBack(_guest, WAIT_FOR_SCREEN_EXIT_KEY);
  }
  _guest.Push(regs.ax);
  _guest.Call(SELECT_SYSTEM_AT_CURSOR);
  regs.ax = _guest.Pop();
}

void AwardArchangelTitle(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = DS.archangelTitle.offset;
  regs.di = DS.commanderRankText.offset;
  regs.cx = RANK_LETTERS;
  do
  {
    SetLow(regs.ax, _guest.Byte(regs.si));
    ++regs.si;
    _guest.SetByte(regs.di, Low(regs.ax));
    ++regs.di;
  } while (--regs.cx != 0);
}

void ShowSellCargoScreen(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  DrawTradeScreenHeader(_guest, DS.sellCargoFrame.offset, SELL_CARGO_HELP_TEXT);
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
      FormatQuantity(_guest, DS.quantityText.offset);
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
    JumpBack(_guest, SELL_CARGO_ROW);
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
  DrawTradeScreenHeader(_guest, DS.buyCargoFrame.offset, BUY_CARGO_HELP_TEXT);
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
      FormatQuantity(_guest, DS.quantityText.offset);
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
      JumpBack(_guest, BUY_CARGO_ROW_END);
    }
    EndTradeRow(_guest);
    if (--regs.cx == 0)
    {
      break;
    }
    JumpBack(_guest, BUY_CARGO_ROW);
  }
  _guest.Set(DS.tradeScreenIsBuy, 1);
  _guest.Set(DS.marketQuantitiesSet, 1);
  _guest.Call(RUN_CARGO_TRADE_MENU);
}

void ShowCommanderStatusScreen(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Get(DS.inFlight) != 1 && _guest.Get(DS.missionNumber) != 0)
  {
    if (_guest.Get(DS.missionStage) == 0)
    {
      _guest.Call(SHOW_MISSION_BRIEFING);
    }
    else if (_guest.Get(DS.missionStage) == STAGE_BRIEFED && _guest.Get(DS.playerDocked) == 1)
    {
      _guest.Call(SHOW_MISSION_DEBRIEFING);
    }
  }
  regs.si = DS.commanderTitle.offset;
  _guest.Call(DRAW_DOCKED_FRAME);
  regs.di = TITLE_CELL;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = DS.defaultCommanderName.offset;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  _guest.Set(DS.textAttribute, STATUS_ATTRIBUTE);
  regs.si = SYSTEM_LABEL_TEXT;
  regs.di = LINE_3;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = _guest.Get(DS.witchspaceCountdown) != 0 ? WITCH_SPACE_TEXT : DS.currentSystemName.offset;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = HYPERSYSTEM_LABEL_TEXT;
  regs.di = LINE_4;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = _guest.Get(DS.witchspaceCountdown) != 0 ? WITCH_SPACE_TEXT : DS.selectedSystemName.offset;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  _guest.Call(FORMAT_FUEL_LIGHT_YEARS);
  regs.di = LINE_5;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = CASH_LABEL_TEXT;
  regs.di = LINE_6;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  // The cash, without its leading spaces.
  regs.si = static_cast<std::uint16_t>(DS.creditBalanceText.offset - 1);
  for (;;)
  {
    ++regs.si;
    if (_guest.Byte(regs.si) != SPACE)
    {
      break;
    }
    JumpBack(_guest, STATUS_CASH_SPACE);
  }
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = LEGAL_STATUS_LABEL_TEXT;
  regs.di = LINE_7;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  SetLow(regs.ax, _guest.Get(DS.legalStatus));
  regs.bx = static_cast<std::uint16_t>(Low(regs.ax) < LEGAL_STATUS_OFFENDER ? 0 : Low(regs.ax) < LEGAL_STATUS_FUGITIVE ? 1 : 2);
  regs.bx = DS.legalStatusNames.At(regs.bx);
  regs.si = _guest.Word(regs.bx);
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = RATING_LABEL_TEXT;
  regs.di = LINE_8;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  // The rating: the first threshold above the kill count.
  regs.si = static_cast<std::uint16_t>(DS.ratingThresholds.offset - 1);
  SetLow(regs.ax, _guest.Get(DS.killCount));
  regs.bx = 0xFFFF;
  for (;;)
  {
    ++regs.si;
    ++regs.bx;
    if (Low(regs.ax) < _guest.Byte(regs.si))
    {
      break;
    }
    JumpBack(_guest, STATUS_RATING);
  }
  regs.bx = DS.ratingNames.At(regs.bx);
  regs.si = _guest.Word(regs.bx);
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = EQUIPMENT_HEADING_TEXT;
  regs.di = LINE_9;
  _guest.Set(DS.textAttribute, EQUIPMENT_HEADING_ATTRIBUTE);
  _guest.Call(PRINT_TEXT_MODE_STRING);
  _guest.Set(DS.textAttribute, STATUS_ATTRIBUTE);
  SetLow(regs.ax, static_cast<std::uint8_t>(_guest.Get(DS.missileCount) + DIGIT_ZERO));
  _guest.Set(DS.data84F5, Low(regs.ax));

  // One row for each item fitted, and after a laser the mounts it is fitted to.
  regs.di = LINE_10;
  regs.cx = EQUIPMENT_ROWS;
  regs.bx = DS.missileCount.offset;
  regs.si = DS.equipmentNames.offset;
  _guest.Set(DS.menuSelectedRow, FIRST_EQUIPMENT_MENU_ROW);
  for (;;)
  {
    for (const std::uint16_t value : {regs.cx, regs.di, regs.bx, regs.si})
    {
      _guest.Push(value);
    }
    if (_guest.Byte(regs.bx) == 0)
    {
      regs.si = Offset(_guest.Pop(), 2);
      regs.bx = Offset(_guest.Pop(), 1);
      regs.di = _guest.Pop();
    }
    else
    {
      regs.si = _guest.Word(regs.si);
      _guest.Call(PRINT_TEXT_MODE_STRING);
      _guest.Call(SELECT_LASER_TYPE);
      if (_guest.Flag(Machine::FLAG_ZERO))
      {
        regs.si = MOUNTS_OPEN_TEXT;
        _guest.Call(PRINT_TEXT_MODE_STRING);
        SetLow(regs.dx, _guest.Get(DS.laserMountsFitted));
        SetHigh(regs.dx, _guest.Get(DS.laserMountTypes));
        regs.bx = DS.laserMountNames.offset;
        regs.cx = LASER_MOUNTS;
        for (;;)
        {
          const bool fitted = (Low(regs.dx) & 1) != 0;
          SetLow(regs.dx, static_cast<std::uint8_t>(Low(regs.dx) >> 1));
          if (fitted)
          {
            SetLow(regs.ax, static_cast<std::uint8_t>(High(regs.dx) & LASER_MOUNT_TYPE_MASK));
            if (Low(regs.ax) == _guest.Get(DS.selectedLaserType))
            {
              regs.si = _guest.Word(regs.bx);
              _guest.Call(PRINT_TEXT_MODE_STRING);
            }
          }
          regs.bx = Offset(regs.bx, 2);
          SetHigh(regs.dx, static_cast<std::uint8_t>(High(regs.dx) >> 2));
          if (--regs.cx == 0)
          {
            break;
          }
          JumpBack(_guest, STATUS_LASER_MOUNT);
        }
        regs.di = static_cast<std::uint16_t>(regs.di - 2);
        regs.si = MOUNTS_CLOSE_TEXT;
        _guest.Call(PRINT_TEXT_MODE_STRING);
      }
      regs.si = Offset(_guest.Pop(), 2);
      regs.bx = Offset(_guest.Pop(), 1);
      regs.di = Offset(_guest.Pop(), TEXT_ROW_BYTES);
    }
    regs.cx = _guest.Pop();
    _guest.Set(DS.menuSelectedRow, static_cast<std::uint8_t>(_guest.Get(DS.menuSelectedRow) + 1));
    if (--regs.cx == 0)
    {
      break;
    }
    JumpBack(_guest, STATUS_EQUIPMENT_ROW);
  }
  SetLow(regs.dx, SCAN_F9);
  WaitForScreenExitKey(_guest);
}

void ShowInventoryScreen(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = DS.inventoryFrame.offset;
  _guest.Call(DRAW_DOCKED_FRAME);
  regs.di = TITLE_CELL;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  // The fuel text without the padding the status screen has: 'Fuel: ', then 'n.n Light Years'.
  _guest.Call(FORMAT_FUEL_LIGHT_YEARS);
  regs.di = LINE_3;
  _guest.SetByte(Offset(regs.si, FUEL_LABEL_BYTES), NUL);
  _guest.Call(PRINT_TEXT_MODE_STRING);
  _guest.SetByte(regs.si, SPACE);
  regs.si = Offset(regs.si, FUEL_DIGITS_AFTER_LABEL);
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = CASH_LABEL_TEXT;
  regs.di = LINE_4;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.di = static_cast<std::uint16_t>(regs.di - INVENTORY_CASH_BACK);
  regs.si = static_cast<std::uint16_t>(DS.creditBalanceText.offset - 1);
  for (;;)
  {
    ++regs.si;
    if (_guest.Byte(regs.si) != SPACE)
    {
      break;
    }
    JumpBack(_guest, INVENTORY_CASH_SPACE);
  }
  _guest.Call(PRINT_TEXT_MODE_STRING);

  // Every product held, and the refugees: name, quantity, unit.
  regs.di = LINE_6;
  regs.si = DS.productNames.offset;
  regs.bx = DS.cargoHold.offset;
  regs.cx = INVENTORY_ROWS;
  for (;;)
  {
    for (const std::uint16_t value : {regs.cx, regs.si, regs.bx, regs.di})
    {
      _guest.Push(value);
    }
    SetLow(regs.ax, _guest.Byte(regs.bx));
    if (Low(regs.ax) == 0)
    {
      regs.di = _guest.Pop();
      JumpBack(_guest, INVENTORY_ROW_END);
    }
    else
    {
      _guest.SetByte(Offset(regs.si, PRODUCT_NAME_END), NUL);
      _guest.Push(regs.ax);
      _guest.Call(PRINT_TEXT_MODE_STRING);
      regs.ax = _guest.Pop();
      _guest.SetByte(regs.si, SPACE);
      _guest.Push(regs.si);
      SetHigh(regs.ax, 0);
      _guest.Push(regs.di);
      FormatQuantity(_guest, INVENTORY_QUANTITY_TEXT);
      regs.di = Offset(_guest.Pop(), 4);
      _guest.Call(PRINT_TEXT_MODE_STRING);
      regs.si = _guest.Pop();
      _guest.Call(PRINT_TEXT_MODE_STRING);
      regs.di = Offset(_guest.Pop(), TEXT_ROW_BYTES);
    }
    regs.bx = Offset(_guest.Pop(), 2);
    regs.si = Offset(_guest.Pop(), PRODUCT_NAME_BYTES);
    regs.cx = _guest.Pop();
    if (--regs.cx == 0)
    {
      break;
    }
    JumpBack(_guest, INVENTORY_ROW);
  }
  SetLow(regs.dx, SCAN_F10);
  WaitForScreenExitKey(_guest);
}

void ShowMissionBriefing(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.missionStage, STAGE_BRIEFED);
  regs.si = DS.emergencyFrame.offset;
  _guest.Call(DRAW_DOCKED_FRAME);
  regs.di = TITLE_CELL;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  const std::uint8_t mission = _guest.Get(DS.missionNumber);
  if (mission == MISSION_SUPERNOVA)
  {
    regs.cx = SUPERNOVA_BRIEFING_LINES;
    regs.di = LINE_4;
    regs.si = DS.supernovaBriefingText.offset;
    _guest.Call(PRINT_TEXT_LINES);
    // Y takes the refugees, N refuses; nothing else answers.
    for (;;)
    {
      WaitForKey(_guest, SUPERNOVA_ANSWER);
      if (High(regs.ax) == SCAN_Y || High(regs.ax) == SCAN_N)
      {
        break;
      }
      JumpBack(_guest, SUPERNOVA_ANSWER);
    }
    if (High(regs.ax) == SCAN_N)
    {
      regs.si = SUPERNOVA_REFUSED_TEXT;
    }
    else
    {
      // The hold is emptied for the refugees.
      regs.di = DS.cargoHold.offset;
      regs.cx = TRADE_ROWS;
      for (;;)
      {
        _guest.SetByte(regs.di, High(regs.cx));
        regs.di = Offset(regs.di, 2);
        if (--regs.cx == 0)
        {
          break;
        }
        JumpBack(_guest, EVACUATE_CARGO);
      }
      _guest.Set(DS.cargoUsedTonnes, REFUGEE_TONNES);
      if (_guest.Get(DS.largeCargoBayFitted) == 1)
      {
        _guest.Set(DS.cargoUsedTonnes, REFUGEE_TONNES_LARGE_BAY);
      }
      SetLow(regs.ax, _guest.Get(DS.cargoUsedTonnes));
      _guest.Set(DS.refugeesTonnes, Low(regs.ax));
      regs.si = SUPERNOVA_ACCEPTED_TEXT;
    }
    regs.di = LINE_10;
    regs.cx = SUPERNOVA_ANSWER_LINES;
    _guest.Call(PRINT_TEXT_LINES);
    _guest.Call(WAIT_FOR_KEY_PRESS);
    _guest.Set(DS.supernovaFrames, SUPERNOVA_DELAY_FRAMES);
    _guest.Set(DS.jumpedSinceBriefing, 0);
    return;
  }
  if (mission == MISSION_MASK_SHIP)
  {
    regs.cx = MASK_BRIEFING_LINES;
    regs.di = LINE_4;
    regs.si = DS.maskBriefingText.offset;
    _guest.Call(PRINT_TEXT_LINES);
    _guest.Call(WAIT_FOR_KEY_PRESS);
    _guest.Set(DS.maskMissionShipsLeft, MASK_SHIPS);
    _guest.Set(DS.maskSystemJumps, MASK_SYSTEM_JUMPS);
    return;
  }
  regs.di = LINE_4;
  regs.si = DS.invasionBriefingText.offset;
  regs.cx = INVASION_BRIEFING_LINES;
  _guest.Call(PRINT_TEXT_LINES);
  _guest.Call(WAIT_FOR_KEY_PRESS);
  _guest.Set(DS.thargoidInvasionActive, 1);
  _guest.Set(DS.jumpedSinceBriefing, 0);
}

void ShowMissionDebriefing(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // Not yet: the mask ship is still about, or the supernova's refugees have not been taken away.
  if (_guest.Get(DS.missionNumber) == MISSION_MASK_SHIP
        ? _guest.Get(DS.maskShipDestroyed) != 1
        : _guest.Get(DS.missionNumber) == MISSION_SUPERNOVA && _guest.Get(DS.jumpedSinceBriefing) != 1)
  {
    return;
  }
  regs.si = DS.taskCompleteFrame.offset;
  _guest.Call(DRAW_DOCKED_FRAME);
  regs.di = TITLE_CELL;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  _guest.Set(DS.missionStage, STAGE_DEBRIEFED);
  const std::uint8_t mission = _guest.Get(DS.missionNumber);
  if (mission == MISSION_SUPERNOVA)
  {
    const bool fullHold = _guest.Get(DS.refugeesTonnes) == REFUGEE_TONNES;
    SetLow(regs.ax, fullHold ? REWARD_DIGIT_FULL_HOLD : REWARD_DIGIT_OTHERWISE);
    regs.bx = fullHold ? REWARD_FULL_HOLD_TENTHS : REWARD_OTHERWISE_TENTHS;
    _guest.Set(DS.refugeeRewardDigit, Low(regs.ax));
    _guest.Set(DS.refugeesTonnes, 0);
    _guest.Set(DS.cargoUsedTonnes, 0);
    regs.ax = regs.bx;
    regs.bx = 0;
    _guest.Call(ADD_CREDITS);
    regs.si = SUPERNOVA_DEBRIEFING_TEXT;
    regs.cx = SUPERNOVA_DEBRIEFING_LINES;
    regs.di = LINE_4;
    _guest.Call(PRINT_TEXT_LINES);
    _guest.Call(WAIT_FOR_KEY_PRESS);
    _guest.Set(DS.missionNumber, 0);
    _guest.Set(DS.missionStage, 0);
    return;
  }
  if (mission == MISSION_MASK_SHIP)
  {
    regs.di = LINE_4;
    if (_guest.Get(DS.maskingDeviceRecovered) == 1)
    {
      _guest.Set(DS.maskingDeviceFitted, 1);
      regs.cx = MASK_RECOVERED_LINES;
      regs.si = MASK_RECOVERED_TEXT;
      JumpBack(_guest, MASK_DEBRIEFING_TEXT);
    }
    else if (_guest.Get(DS.fledMaskShip) == 1)
    {
      regs.cx = MASK_FLED_LINES;
      regs.si = MASK_FLED_TEXT;
    }
    else
    {
      regs.cx = MASK_DESTROYED_LINES;
      regs.si = MASK_DESTROYED_TEXT;
    }
    _guest.Call(PRINT_TEXT_LINES);
    _guest.Call(WAIT_FOR_KEY_PRESS);
    _guest.Set(DS.maskMissionShipsLeft, 0);
    _guest.Set(DS.maskShipDestroyed, 0);
    _guest.Set(DS.missionNumber, 0);
    _guest.Set(DS.maskingDeviceRecovered, 0);
    _guest.Set(DS.maskSystemJumps, 0);
    _guest.Set(DS.fledMaskShip, 0);
    _guest.Set(DS.missionStage, 0);
    return;
  }
  regs.di = LINE_4;
  regs.si = DS.invasionDebriefText.offset;
  regs.cx = INVASION_DEBRIEFING_LINES;
  _guest.Call(PRINT_TEXT_LINES);
  _guest.Call(WAIT_FOR_KEY_PRESS);
  _guest.Call(AWARD_ARCHANGEL_TITLE);
  _guest.Set(DS.antiEcmEmulatorFitted, 1);
  _guest.Set(DS.missionNumber, 0);
  _guest.Set(DS.missionStage, 0);
  _guest.Set(DS.invadedStationDestroyed, 0);
  _guest.Set(DS.thargoidInvasionActive, 0);
  _guest.Set(DS.missionStage, 0);
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
    JumpBack(_guest, DOCKED_DISK_MENU_ENTRY);
    DispatchDockedKeys(_guest, SHOW_DISC_CONTROL_SCREEN);
    return;
  }
  JumpBack(_guest, DOCKED_STATUS_ENTRY);
  DispatchDockedKeys(_guest, SHOW_COMMANDER_STATUS_SCREEN);
}

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

constexpr Machine::NativeReturn NEAR = Machine::NativeReturn::Near;
constexpr Machine::NativeWait ALWAYS = Machine::NativeWait::Always;

constexpr std::array ENTRIES = {
  NativeEntry{0x49E4, "AwardArchangelTitle", &AwardArchangelTitle, CLOBBERS_AX_CX_SI_DI},
  NativeEntry{0x5A30, "ShowSellCargoScreen", &ShowSellCargoScreen, CLOBBERS_ALL, NEAR, 0, ALWAYS},
  NativeEntry{0x5AE9, "ShowBuyCargoScreen", &ShowBuyCargoScreen, CLOBBERS_ALL, NEAR, 0, ALWAYS},
  NativeEntry{0x5EA9, "ShowCommanderStatusScreen", &ShowCommanderStatusScreen, CLOBBERS_ALL, NEAR, 0, ALWAYS},
  NativeEntry{0x6020, "ShowInventoryScreen", &ShowInventoryScreen, CLOBBERS_ALL, NEAR, 0, ALWAYS},
  NativeEntry{0x658F, "PrintCreditsOnMessageLine", &PrintCreditsOnMessageLine, PRESERVES_ALL},
  NativeEntry{0x6923, "FormatFuelLightYears", &FormatFuelLightYears, CLOBBERS_AX_BX_DI},
  NativeEntry{0x6DF2, "ShowMissionBriefing", &ShowMissionBriefing, CLOBBERS_ALL, NEAR, 0, ALWAYS},
  NativeEntry{0x6EB7, "ShowMissionDebriefing", &ShowMissionDebriefing, CLOBBERS_ALL, NEAR, 0, ALWAYS},
  NativeEntry{0x7C88, "DrawDockedFrame", &DrawDockedFrame, PRESERVES_ALL},
  NativeEntry{0x7CE9, "DrawFrameSides", &DrawFrameSides, PRESERVES_ALL},
  NativeEntry{0x7CF8, "DrawFrameRow", &DrawFrameRow, PRESERVES_ALL},
  NativeEntry{0x7D81, "RunTitleAndDocked", &RunTitleAndDocked, CLOBBERS_ALL, NEAR, 0, ALWAYS},
};

} // namespace

std::span<const NativeEntry> DockedEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
