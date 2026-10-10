#include "pch.h"

#include "Docked.h"
#include "Galaxy.h"

#include "Arithmetic.h"
#include "DataOverlay.h"

#include <initializer_list>

namespace Elite
{

namespace
{

// The original routines these call, which other subsystems port.
constexpr std::uint16_t PRESENT_CHART_FRAME = 0x0587;
constexpr std::uint16_t PLOT_PIXEL = 0x15E0;
constexpr std::uint16_t DRAW_CLIPPED_LINE = 0x1603;
constexpr std::uint16_t DRAW_LINE = 0x16D1;
constexpr std::uint16_t DRAW_DISC = 0x1826;
constexpr std::uint16_t DRAW_SCREEN_STRING = 0x32D8;
constexpr std::uint16_t FORMAT_DECIMAL_5 = 0x3407;
constexpr std::uint16_t BLANK_LEADING_ZEROS = 0x3432;
constexpr std::uint16_t DRAW_SMALL_VIEW_STRING = 0x3527;
constexpr std::uint16_t PRINT_TEXT_MODE_STRING = 0x60D2;
constexpr std::uint16_t READ_FIRE_BUTTON = 0x74E0;
constexpr std::uint16_t READ_STEERING = 0x7536;
constexpr std::uint16_t GET_KEY = 0x7616;
constexpr std::uint16_t READ_TEXT_LINE = 0x7694;
constexpr std::uint16_t DRAW_CHART_FRAME = 0x7C25;
constexpr std::uint16_t DRAW_DOCKED_FRAME = 0x7C88;

// This subsystem's routines, which the routines that wait call through their entries.
constexpr std::uint16_t GET_SHORT_RANGE_OFFSET = 0x1076;
constexpr std::uint16_t TWIST_SYSTEM_SEEDS = 0x10C0;
constexpr std::uint16_t LOAD_GALAXY_SEEDS = 0x10D6;
constexpr std::uint16_t MOVE_CURSOR_TO_SYSTEM = 0x1146;
constexpr std::uint16_t SELECT_SYSTEM_AT_CURSOR = 0x1199;
constexpr std::uint16_t SHOW_NEAREST_SYSTEM_DISTANCE = 0x1341;
constexpr std::uint16_t LOAD_SYSTEM_SEEDS = 0x139C;
constexpr std::uint16_t ADVANCE_TO_NEXT_SYSTEM = 0x13B4;
constexpr std::uint16_t GENERATE_SYSTEM_NAME = 0x13C1;
constexpr std::uint16_t FIND_SYSTEM_BY_NAME = 0x140D;
constexpr std::uint16_t DRAW_CHART_ITEMS = 0x14C5;
constexpr std::uint16_t PLACE_CHART_LABELS = 0x1505;
constexpr std::uint16_t CLEAR_CHART_TEXT_LINES = 0x15BC;
constexpr std::uint16_t TERMINATE_SELECTED_SYSTEM_NAME = 0x60EB;
constexpr std::uint16_t FORMAT_SELECTED_SYSTEM_DISTANCE = 0x60F7;
constexpr std::uint16_t SHOW_SYSTEM_DESCRIPTION = 0x6FC0;

// Where the loops of the routines that wait jump back to.
constexpr std::uint16_t GALACTIC_CHART_FRAME = 0x0CF9;
constexpr std::uint16_t GALACTIC_SYSTEM_DOT = 0x0D92;
constexpr std::uint16_t GALACTIC_KEY_IGNORED = 0x0E0A;
constexpr std::uint16_t SHORT_RANGE_NEXT_SYSTEM = 0x0E96;
constexpr std::uint16_t SHORT_RANGE_LABEL_BELOW_TOP = 0x0F23;
constexpr std::uint16_t SHORT_RANGE_LABEL_ABOVE_BOTTOM = 0x0F2D;
constexpr std::uint16_t SHORT_RANGE_LABEL_NAME = 0x0F41;
constexpr std::uint16_t SHORT_RANGE_SYSTEM_DONE = 0x0F50;
constexpr std::uint16_t SHORT_RANGE_CHART_FRAME = 0x0F60;
constexpr std::uint16_t SHORT_RANGE_KEY_IGNORED = 0x1030;
constexpr std::uint16_t FIND_UPPER_CASE = 0x144F;
constexpr std::uint16_t FIND_NEXT_SYSTEM = 0x146F;
constexpr std::uint16_t FIND_NOT_ON_MAP = 0x148B;

// Scan codes the charts and the data screen read.
constexpr std::uint8_t SCAN_ESCAPE = 0x01;
constexpr std::uint8_t SCAN_D = 0x20;
constexpr std::uint8_t SCAN_F = 0x21;
constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_F5 = 0x3F;
constexpr std::uint8_t SCAN_F6 = 0x40;
constexpr std::uint8_t SCAN_F7 = 0x41;
constexpr std::uint8_t SCAN_F10 = 0x44;
constexpr std::uint8_t SCAN_KEYPAD_5 = 0x4C;

// The control codes' handlers as textControlCodes holds them, and where they return to.
constexpr std::uint16_t INSERT_SYSTEM_NAME = 0x707A;
constexpr std::uint16_t INSERT_SYSTEM_ADJECTIVE = 0x708D;
constexpr std::uint16_t INSERT_RANDOM_NAME = 0x70C1;
constexpr std::uint16_t BACKSPACE_DESCRIPTION = 0x7107;
constexpr std::uint16_t START_CAPITALIZING = 0x7109;
constexpr std::uint16_t STOP_CAPITALIZING = 0x710F;
constexpr std::uint16_t RESUME_TEXT_EXPANSION = 0x7051;

constexpr std::uint8_t SPACE = 0x20;
constexpr std::uint8_t DECIMAL_POINT = 0x2E;

// The short-range chart: half its width and height in galaxy units, and its centre in chart pixels.
constexpr std::uint16_t SHORT_RANGE_HALF_WIDTH = 0x14;
constexpr std::uint16_t SHORT_RANGE_HALF_HEIGHT = 0x12;
constexpr std::uint16_t SHORT_RANGE_CENTER_X = 0x50;
constexpr std::uint16_t SHORT_RANGE_CENTER_ROW = 0x40;
constexpr std::uint8_t SHORT_RANGE_DIVISOR = 7;

constexpr std::uint16_t GALAXY_SYSTEMS = 0x100;
constexpr std::uint8_t GALAXY_SEED_BYTES = 6;
constexpr std::uint8_t CHART_ITEM_BYTES = 8;
constexpr std::uint8_t CHART_DISC_COLOR = 3;
constexpr std::uint8_t LABEL_TRIES = 0x23;
constexpr std::uint8_t LABEL_LOWEST_ROW = 0x7C;

// The text lines under a chart, as CGA offsets.
constexpr std::uint16_t CHART_TEXT_LINE_1 = 0x1B8A;
constexpr std::uint16_t CHART_TEXT_LINE_2 = 0x1D1A;
constexpr std::uint16_t CHART_NAME_PADDING = 0x1B9A; // past the eight characters of a name on line 1
constexpr std::uint16_t CHART_TITLE = 0x0388;

// Inks as CGA pixel patterns: colours 1, 2 and 3.
constexpr std::uint16_t INK_1 = 0x5555;
constexpr std::uint16_t INK_2 = 0xAAAA;
constexpr std::uint16_t INK_3 = 0xFFFF;

// The charts' crosses: half their arms, in pixels.
constexpr std::uint16_t CURRENT_SYSTEM_ARM = 0x11;
constexpr std::uint16_t CURSOR_ARM = 5;
constexpr std::uint8_t CURSOR_LOWEST_ROW = 0x7F;
constexpr std::uint8_t CHART_ROWS = 0x80;
constexpr std::uint8_t LABEL_OFFSET_X = 7;
constexpr std::uint8_t LABEL_ABOVE = 2;
constexpr std::uint8_t LABEL_BELOW = 3;
constexpr std::uint8_t SMALL_LETTER_PIXELS = 6;

// distanceText's digits: hundreds, tens and units, then the tenths past the point.
constexpr std::uint16_t DISTANCE_TEXT_HUNDREDS = 0x0A;
constexpr std::uint16_t DISTANCE_TEXT_TENS = 0x0B;
constexpr std::uint16_t DISTANCE_TEXT_UNITS = 0x0C;
constexpr std::uint16_t DISTANCE_TEXT_TENTHS = 0x0E;
constexpr std::uint8_t UPPER_CASE_MASK = 0xDF;

// The data screen's labels and figures in the data segment, and where it prints them (text offsets).
constexpr std::uint16_t DATA_TITLE = 0x0054;
constexpr std::uint16_t DISTANCE_LABEL = 0x7D4B; // 'Distance:     '
constexpr std::uint16_t DISTANCE_ROW = 0x0144;
constexpr std::uint16_t LIGHT_YEARS_LABEL = 0x7D61; // ' Light Years'
constexpr std::uint16_t ECONOMY_LABEL = 0x7C1C;     // 'Economy:      '
constexpr std::uint16_t ECONOMY_ROW = 0x01E4;
constexpr std::uint16_t GOVERNMENT_LABEL = 0x7CCD; // 'Government:   '
constexpr std::uint16_t GOVERNMENT_ROW = 0x0284;
constexpr std::uint16_t TECH_LEVEL_LABEL = 0x7D39; // 'Tech. Level:  '
constexpr std::uint16_t TECH_LEVEL_ROW = 0x0324;
constexpr std::uint16_t POPULATION_DIGITS = 0x7D7D; // '000.0 Billion'
constexpr std::uint16_t POPULATION_LABEL = 0x7D6E;  // 'Population:   '
constexpr std::uint16_t POPULATION_ROW = 0x03C4;
constexpr std::uint16_t POPULATION_SHOWN = 0x7D7F; // the units digit, where the point goes after it
constexpr std::uint16_t SPECIES_OPEN = 0x7D8B;     // '('
constexpr std::uint16_t SPECIES_ROW = 0x0414;
constexpr std::uint16_t HUMAN_COLONIALS = 0x7D8D; // 'Human Colonials)'
constexpr std::uint8_t NO_SPECIES = 0xFF;
constexpr std::uint16_t PRODUCTIVITY_LABEL = 0x7E94; // 'Productivity: '
constexpr std::uint16_t PRODUCTIVITY_ROW = 0x04B4;
constexpr std::uint16_t RADIUS_LABEL = 0x7EAE; // 'Radius (Av): 00000 km'
constexpr std::uint16_t RADIUS_ROW = 0x0554;
constexpr std::uint8_t TECH_LEVEL_TENS = 10;

// FormatSelectedSystemDistance's text ends here, its tenths digit last.
constexpr std::uint16_t SELECTED_DISTANCE_TEXT_END = 0x7D5F;
// Where InsertRandomName keeps selectedSystemName while it borrows it.
constexpr std::uint16_t SAVED_SYSTEM_NAME = 0x9650;
constexpr std::uint16_t SYSTEM_NAME_BYTES = 8;
constexpr std::uint16_t ADJECTIVE_SUFFIX_BYTES = 5;

constexpr std::uint16_t DESCRIPTION_BUFFER_WORDS = 0x80;
constexpr std::uint16_t DESCRIPTION_SCREEN_OFFSET = 0x5F4;
constexpr std::uint16_t DESCRIPTION_LINE_CHARACTERS = 0x24;
constexpr std::uint16_t TEXT_ROW_BYTES = 0x50;
constexpr std::uint8_t DESCRIPTION_PHRASE_CODE = 0x80;
constexpr std::uint8_t DESCRIPTION_PHRASE_DIVISOR = 0x34;

[[nodiscard]] constexpr std::uint16_t MakeWord(std::uint8_t _low, std::uint8_t _high) noexcept
{
  return static_cast<std::uint16_t>(_low | (_high << 8));
}

[[nodiscard]] bool Carry(const Machine::Registers& _regs) noexcept
{
  return (_regs.flags & Machine::FLAG_CARRY) != 0;
}

// The difference of two bytes as a word, then made positive with neg.
[[nodiscard]] std::uint16_t Magnitude(std::uint16_t _value) noexcept
{
  return (_value & 0x8000) != 0 ? static_cast<std::uint16_t>(0u - _value) : _value;
}

// mul al.
[[nodiscard]] std::uint16_t Square(std::uint8_t _value) noexcept
{
  return static_cast<std::uint16_t>(_value * _value);
}

// The step STOSW takes, backwards with the direction flag set.
[[nodiscard]] std::uint16_t WordStep(const Machine::Registers& _regs) noexcept
{
  return (_regs.flags & Machine::FLAG_DIRECTION) != 0 ? static_cast<std::uint16_t>(0xFFFE) : static_cast<std::uint16_t>(2);
}

// The galaxy-unit offset of a short-range chart coordinate from the chart's centre: shl ax,1 then idiv
// by 7 into AL (it cannot overflow), cbw, from _origin, clamped at 0.
[[nodiscard]] std::uint16_t ChartToGalaxy(std::uint16_t _fromCenter, std::uint8_t _origin) noexcept
{
  const auto doubled = static_cast<std::int16_t>(_fromCenter << 1);
  const auto quotient = static_cast<std::int8_t>(doubled / SHORT_RANGE_DIVISOR);
  const auto position = static_cast<std::uint16_t>(quotient + _origin);
  return (position & 0x8000) != 0 ? static_cast<std::uint16_t>(0) : position;
}

// 3.5 times a galaxy-unit offset (x*2 + x + x sar 1), from the chart's centre.
[[nodiscard]] std::uint16_t GalaxyToChart(std::uint16_t _offset, std::uint16_t _center) noexcept
{
  const auto half = static_cast<std::uint16_t>(static_cast<std::int16_t>(_offset) >> 1);
  return static_cast<std::uint16_t>(_offset * 3 + half + _center);
}

// The loop of byte moves (mov al,[si]; mov [di],al; inc si; inc di; loop) some routines make.
void CopyBytes(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  do
  {
    SetLow(regs.ax, _guest.Byte(regs.si));
    _guest.SetByte(regs.di, Low(regs.ax));
    ++regs.si;
    ++regs.di;
  } while (--regs.cx != 0);
}

// One pass of PlaceChartLabels over chartItems: CF from the first item the label at DL/DH, BL/BH
// overlaps, with DI on it.
[[nodiscard]] bool AnyChartItemOverlaps(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = DS.chartItems.offset;
  regs.cx = _guest.Get(DS.chartItemCount);
  do
  {
    ChartItemOverlaps(_guest);
    if (Carry(regs))
    {
      return true;
    }
    regs.di = static_cast<std::uint16_t>(regs.di + CHART_ITEM_BYTES);
  } while (--regs.cx != 0);
  return false;
}

// jmp [bx] to a control code's handler, with SI and the return to ResumeTextExpansion pushed; the
// handler returns there, which pops SI.
void RunTextControlCode(Guest& _guest, std::uint16_t _handler)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t text = regs.si;
  switch (_handler)
  {
  case INSERT_SYSTEM_NAME:
    InsertSystemName(_guest);
    break;
  case INSERT_SYSTEM_ADJECTIVE:
    InsertSystemAdjective(_guest);
    break;
  case INSERT_RANDOM_NAME:
    InsertRandomName(_guest);
    break;
  case BACKSPACE_DESCRIPTION:
    BackspaceDescription(_guest);
    break;
  case START_CAPITALIZING:
    StartCapitalizing(_guest);
    break;
  case STOP_CAPITALIZING:
    StopCapitalizing(_guest);
    break;
  default:
    // Codes 7-31 jump into descriptionPhraseLists, which no text uses: run whatever is there.
    _guest.Push(text);
    _guest.Call(_handler);
    regs.si = _guest.Pop();
    return;
  }
  regs.si = text;
}

// A chart's cross (CS:0D33, CS:0D61, CS:0FAD): _arm either way of (DX, BX) across and down, as two clipped
// lines, with CX = DX and AX = BX on entry.
void DrawCross(Guest& _guest, std::uint16_t _arm)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Push(regs.bx);
  _guest.Push(regs.dx);
  regs.cx = static_cast<std::uint16_t>(regs.cx - _arm);
  regs.dx = static_cast<std::uint16_t>(regs.dx + _arm);
  _guest.Call(DRAW_CLIPPED_LINE);
  regs.dx = _guest.Pop();
  regs.bx = _guest.Pop();
  regs.ax = regs.bx;
  regs.cx = regs.dx;
  regs.ax = static_cast<std::uint16_t>(regs.ax - _arm);
  regs.bx = static_cast<std::uint16_t>(regs.bx + _arm);
  _guest.Call(DRAW_CLIPPED_LINE);
}

// The chart cursor's cross and the dot at its centre, in colour 1 then 0 (CS:0D4D, CS:0F99).
void DrawChartCursor(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.drawColor, 1);
  regs.cx = _guest.Get(DS.chartCursorX);
  regs.dx = regs.cx;
  regs.ax = _guest.Get(DS.chartCursorY);
  regs.bx = regs.ax;
  DrawCross(_guest, CURSOR_ARM);
  _guest.Set(DS.drawColor, 0);
  regs.dx = Join(_guest.Get(DS.chartCursorY), _guest.Get(DS.chartCursorX));
  _guest.Call(PLOT_PIXEL);
}

// The steering moves a chart's cursor (CS:0DB4, CS:0FDA): AL across, clamped to the chart, and AH negated
// down, clamped to its 128 rows.
void MoveChartCursor(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(READ_STEERING);
  SetHigh(regs.ax, Negate(High(regs.ax)));
  const std::uint8_t across = Low(regs.ax);
  const unsigned x = unsigned{across} + unsigned{_guest.Get(DS.chartCursorX)};
  if ((across & 0x80) == 0)
  {
    SetLow(regs.ax, x > 0xFF ? std::uint8_t{0xFF} : static_cast<std::uint8_t>(x));
  }
  else
  {
    // Adding a negative byte carries unless it goes below 0.
    SetLow(regs.ax, x > 0xFF ? static_cast<std::uint8_t>(x) : std::uint8_t{0});
  }
  _guest.Set(DS.chartCursorX, Low(regs.ax));
  const std::uint8_t down = High(regs.ax);
  const unsigned y = unsigned{down} + unsigned{_guest.Get(DS.chartCursorY)};
  if ((down & 0x80) == 0)
  {
    const auto row = static_cast<std::uint8_t>(y);
    SetHigh(regs.ax, row < CHART_ROWS ? row : CURSOR_LOWEST_ROW);
  }
  else
  {
    SetHigh(regs.ax, y > 0xFF ? static_cast<std::uint8_t>(y) : std::uint8_t{0});
  }
  _guest.Set(DS.chartCursorY, High(regs.ax));
}

// What tells the two charts' key handling apart.
struct ChartKeys
{
  std::uint16_t frame;   // the start of the chart's frame, where its loop jumps back to
  std::uint16_t ignored; // the jump to it that the key tests reach (CS:0E0A, CS:1030)
  std::uint8_t ownKey;   // the chart's own F key, which leaves it open
  void (*recenter)(Guest&);
  DataField<std::uint8_t> keptCursorX;
  DataField<std::uint8_t> keptCursorY;
};

// The galactic chart's cursor back on the current system, through AL (CS:0E19).
void RecenterGalacticCursor(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  SetLow(regs.ax, _guest.Get(DS.currentSystemX));
  _guest.Set(DS.chartCursorX, Low(regs.ax));
  SetLow(regs.ax, _guest.Get(DS.currentSystemChartY));
  _guest.Set(DS.chartCursorY, Low(regs.ax));
}

// The short-range chart's cursor back on its centre (CS:103F).
void RecenterShortRangeCursor(Guest& _guest)
{
  _guest.Set(DS.chartCursorX, static_cast<std::uint8_t>(SHORT_RANGE_CENTER_X));
  _guest.Set(DS.chartCursorY, static_cast<std::uint8_t>(SHORT_RANGE_CENTER_ROW));
}

constexpr ChartKeys GALACTIC_CHART_KEYS{.frame = GALACTIC_CHART_FRAME,
                                        .ignored = GALACTIC_KEY_IGNORED,
                                        .ownKey = SCAN_F5,
                                        .recenter = &RecenterGalacticCursor,
                                        .keptCursorX = DS.galacticCursorX,
                                        .keptCursorY = DS.galacticCursorY};
constexpr ChartKeys SHORT_RANGE_CHART_KEYS{.frame = SHORT_RANGE_CHART_FRAME,
                                           .ignored = SHORT_RANGE_KEY_IGNORED,
                                           .ownKey = SCAN_F6,
                                           .recenter = &RecenterShortRangeCursor,
                                           .keptCursorX = DS.shortRangeCursorX,
                                           .keptCursorY = DS.shortRangeCursorY};

// A chart's key (CS:0DEF, CS:1015): D shows the distance to the system nearest the cursor, F finds one by
// name, keypad 5 or fire recentres the cursor, and Esc or an F key other than the chart's own closes it,
// selecting the system at the cursor unless the hyperspace countdown runs. True when it closes, with the
// key in AX; otherwise it jumps back to the chart's frame.
[[nodiscard]] bool ReadChartKey(Guest& _guest, const ChartKeys& _chart)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(GET_KEY);
  if (_guest.Flag(Machine::FLAG_ZERO))
  {
    _guest.JumpBack(_chart.frame);
    return false;
  }
  const std::uint8_t key = High(regs.ax);
  if (key == SCAN_D || key == SCAN_F)
  {
    _guest.Call(key == SCAN_D ? SHOW_NEAREST_SYSTEM_DISTANCE : FIND_SYSTEM_BY_NAME);
    _guest.JumpBack(_chart.frame);
    return false;
  }
  bool recenter = key == SCAN_KEYPAD_5;
  if (!recenter)
  {
    _guest.Push(regs.ax);
    _guest.Call(READ_FIRE_BUTTON);
    regs.ax = _guest.Pop();
    recenter = _guest.Flag(Machine::FLAG_CARRY);
  }
  if (recenter)
  {
    _chart.recenter(_guest);
  }
  const bool closes = key == SCAN_ESCAPE || (key >= SCAN_F1 && key <= SCAN_F10 && key != _chart.ownKey);
  if (!closes)
  {
    _guest.JumpBack(_chart.ignored);
    _guest.JumpBack(_chart.frame);
    return false;
  }
  _guest.Push(regs.ax);
  if (_guest.Get(DS.hyperspaceCountdown) == 0)
  {
    _guest.Call(SELECT_SYSTEM_AT_CURSOR);
    SetLow(regs.ax, _guest.Get(DS.chartCursorX));
    _guest.Set(_chart.keptCursorX, Low(regs.ax));
    SetLow(regs.ax, _guest.Get(DS.chartCursorY));
    _guest.Set(_chart.keptCursorY, Low(regs.ax));
  }
  regs.ax = _guest.Pop();
  return true;
}

// One frame of the galactic chart up to its presentation (CS:0CF9): the fuel range about the current
// system, its cross, the cursor, and a dot for each of the galaxy's 256 systems.
void DrawGalacticChart(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(LOAD_GALAXY_SEEDS);
  regs.bx = static_cast<std::uint16_t>((_guest.Get(DS.fuel) >> 3) + 3);
  regs.dx = _guest.Get(DS.currentSystemX);
  regs.cx = _guest.Get(DS.currentSystemChartY);
  _guest.Set(DS.drawColor, 2);
  _guest.Call(DRAW_DISC);
  _guest.Set(DS.drawColor, 0);
  regs.cx = _guest.Get(DS.currentSystemX);
  regs.dx = regs.cx;
  regs.ax = _guest.Get(DS.currentSystemChartY);
  regs.bx = regs.ax;
  DrawCross(_guest, CURRENT_SYSTEM_ARM);
  DrawChartCursor(_guest);
  _guest.Set(DS.drawColor, CHART_DISC_COLOR);
  SetLow(regs.ax, 0);
  for (;;)
  {
    _guest.Push(regs.ax);
    regs.dx = Join(static_cast<std::uint8_t>(_guest.Get(DS.systemY) >> 1), _guest.Get(DS.systemX));
    _guest.Call(PLOT_PIXEL);
    _guest.Call(TWIST_SYSTEM_SEEDS);
    _guest.Call(TWIST_SYSTEM_SEEDS);
    _guest.Call(TWIST_SYSTEM_SEEDS);
    _guest.Call(TWIST_SYSTEM_SEEDS);
    regs.ax = _guest.Pop();
    SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) + 1));
    if (Low(regs.ax) == 0)
    {
      return;
    }
    _guest.JumpBack(GALACTIC_SYSTEM_DOT);
  }
}

// One frame of the short-range chart up to its presentation (CS:0F60): the fuel range about the centre, the
// current system's cross, the chart's discs and labels, and the cursor.
void DrawShortRangeChart(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = static_cast<std::uint16_t>(_guest.Get(DS.fuel) >> 1);
  regs.dx = SHORT_RANGE_CENTER_X;
  regs.cx = SHORT_RANGE_CENTER_ROW;
  _guest.Set(DS.drawColor, 2);
  _guest.Call(DRAW_DISC);
  _guest.Set(DS.drawColor, 0);
  // The current system's cross, from (50h, 2Fh) to (50h, 51h) and from (3Fh, 40h) to (61h, 40h).
  regs.cx = 0x2F50;
  regs.dx = 0x5150;
  _guest.Call(DRAW_LINE);
  regs.cx = 0x403F;
  regs.dx = 0x4061;
  _guest.Call(DRAW_LINE);
  _guest.Set(DS.drawColor, CHART_DISC_COLOR);
  _guest.Call(DRAW_CHART_ITEMS);
  DrawChartCursor(_guest);
}

// ShowShortRangeChart's item for the system in systemSeeds (CS:0E9F), DX and CX its offsets from the
// current system: a disc of radius 4 or 6 at 3.5 times them about the centre, and a label to place to its
// right, the name GenerateSystemName makes. The seeds are left on the next system.
void AddShortRangeSystem(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = regs.cx;
  regs.cx = static_cast<std::uint16_t>((regs.cx << 1) + regs.ax);
  regs.ax = Sar(regs.ax, 1);
  regs.cx = static_cast<std::uint16_t>(regs.cx + regs.ax);
  regs.ax = regs.dx;
  regs.dx = static_cast<std::uint16_t>((regs.dx << 1) + regs.ax);
  regs.ax = Sar(regs.ax, 1);
  regs.dx = static_cast<std::uint16_t>(regs.dx + regs.ax + SHORT_RANGE_CENTER_X);
  regs.cx = static_cast<std::uint16_t>(regs.cx + SHORT_RANGE_CENTER_ROW);
  _guest.Push(regs.cx);
  _guest.Push(regs.dx);
  regs.di = _guest.Get(DS.chartItemEnd);
  regs.bx = Join(Low(regs.cx), Low(regs.dx));
  _guest.SetWord(Offset(regs.di, 4), regs.bx);
  regs.bx = static_cast<std::uint16_t>(((_guest.Get(DS.systemY) & 1) << 1) + 4);
  _guest.SetWord(Offset(regs.di, 6), regs.bx);
  // Its box: half the radius either side, as add al,dl / xchg / sub al,dl / neg al make it.
  for (const std::uint8_t center : {Low(regs.dx), Low(regs.cx)})
  {
    const auto half = static_cast<std::uint8_t>(regs.bx >> 1);
    regs.ax = Join(static_cast<std::uint8_t>(half + center), static_cast<std::uint8_t>(center - half));
    _guest.SetWord(regs.di, regs.ax);
    regs.di = Offset(regs.di, 2);
  }
  regs.di = Offset(regs.di, CHART_ITEM_BYTES - 4);
  _guest.Set(DS.chartItemEnd, regs.di);
  _guest.Set(DS.chartItemCount, static_cast<std::uint8_t>(_guest.Get(DS.chartItemCount) + 1));
  _guest.Call(GENERATE_SYSTEM_NAME);
  regs.cx = High(regs.dx);
  regs.ax = static_cast<std::uint16_t>(SMALL_LETTER_PIXELS * Low(regs.cx));
  regs.dx = _guest.Pop();
  SetLow(regs.dx, static_cast<std::uint8_t>(Low(regs.dx) + LABEL_OFFSET_X));
  SetHigh(regs.dx, static_cast<std::uint8_t>(Low(regs.dx) + Low(regs.ax)));
  regs.di = _guest.Get(DS.chartLabelCursor);
  _guest.SetWord(regs.di, regs.dx);
  regs.bx = _guest.Pop();
  SetHigh(regs.bx, Low(regs.bx));
  SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) - LABEL_ABOVE));
  if ((Low(regs.bx) & 0x80) != 0)
  {
    // A label above the chart's top would loop here for ever: the jump goes back past the test. No system
    // on the chart is near enough the top (its rows are 4-7Bh).
    for (;;)
    {
      SetHigh(regs.bx, static_cast<std::uint8_t>(High(regs.bx) + 1));
      SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) + 1));
      _guest.JumpBack(SHORT_RANGE_LABEL_BELOW_TOP);
    }
  }
  SetHigh(regs.bx, static_cast<std::uint8_t>(High(regs.bx) + LABEL_BELOW));
  while (High(regs.bx) >= CHART_ROWS)
  {
    SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) - 1));
    SetHigh(regs.bx, static_cast<std::uint8_t>(High(regs.bx) - 1));
    _guest.JumpBack(SHORT_RANGE_LABEL_ABOVE_BOTTOM);
  }
  _guest.SetWord(Offset(regs.di, 2), regs.bx);
  regs.di = Offset(regs.di, 4);
  regs.si = DS.selectedSystemName.offset;
  for (;;)
  {
    SetLow(regs.ax, _guest.Byte(regs.si));
    _guest.SetByte(regs.di, Low(regs.ax));
    ++regs.si;
    ++regs.di;
    if (--regs.cx == 0)
    {
      break;
    }
    _guest.JumpBack(SHORT_RANGE_LABEL_NAME);
  }
  _guest.SetByte(regs.di, High(regs.cx));
  ++regs.di;
  _guest.Set(DS.chartLabelCursor, regs.di);
}

// REPE CMPSB: DS:SI with ES:DI for at most CX bytes, in DF's direction. ZF set when every byte compared
// was equal; returned.
bool CompareBytes(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (regs.cx == 0)
  {
    return _guest.Flag(Machine::FLAG_ZERO);
  }
  const auto step = static_cast<std::uint16_t>(_guest.Flag(Machine::FLAG_DIRECTION) ? 0xFFFF : 1);
  bool equal = true;
  do
  {
    const std::uint8_t source = _guest.Byte(regs.si);
    const std::uint8_t destination = _guest.FarByte(regs.es, regs.di);
    equal = source == destination;
    _guest.SetFlag(Machine::FLAG_CARRY, source < destination);
    regs.si = Offset(regs.si, step);
    regs.di = Offset(regs.di, step);
    --regs.cx;
  } while (regs.cx != 0 && equal);
  _guest.SetFlag(Machine::FLAG_ZERO, equal);
  return equal;
}

// FindSystemByName's 'ERROR: <name> not on map!' on the second line under the chart (CS:148B).
void ShowNotOnMap(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = DS.findErrorPrefix.offset;
  regs.di = CHART_TEXT_LINE_2;
  regs.bx = INK_3;
  _guest.Call(DRAW_SCREEN_STRING);
  regs.si = DS.findInputEcho.offset;
  regs.bx = INK_2;
  _guest.Call(DRAW_SCREEN_STRING);
  regs.si = DS.findErrorSuffix.offset;
  regs.bx = INK_3;
  _guest.Call(DRAW_SCREEN_STRING);
}

// PrintTextModeString of the text whose pointer is _bytes into _table, with AX = _bytes.
void PrintNamed(Guest& _guest, std::uint16_t _table, std::uint16_t _bytes)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = _bytes;
  regs.bx = Offset(_table, regs.ax);
  regs.si = _guest.Word(regs.bx);
  _guest.Call(PRINT_TEXT_MODE_STRING);
}

} // namespace

void ShowGalacticChart(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.sunFringeMask, 0);
  SetLow(regs.cx, _guest.Get(DS.currentSystemIndex));
  _guest.Call(LOAD_SYSTEM_SEEDS);
  SetLow(regs.ax, _guest.Get(DS.systemX));
  _guest.Set(DS.currentSystemX, Low(regs.ax));
  SetLow(regs.ax, static_cast<std::uint8_t>(_guest.Get(DS.systemY) >> 1));
  _guest.Set(DS.currentSystemChartY, Low(regs.ax));
  _guest.Call(DRAW_CHART_FRAME);
  SetLow(regs.ax, static_cast<std::uint8_t>(_guest.Get(DS.galaxyNumber) + '1'));
  _guest.Set(DS.galacticChartNumber, Low(regs.ax));
  regs.si = DS.galacticChartTitle.offset;
  regs.di = CHART_TITLE;
  _guest.Set(DS.textPaperPattern, 0);
  regs.bx = INK_1;
  _guest.Call(DRAW_SCREEN_STRING);
  _guest.Call(CLEAR_CHART_TEXT_LINES);
  SetLow(regs.ax, _guest.Get(DS.galacticCursorX));
  _guest.Set(DS.chartCursorX, Low(regs.ax));
  SetLow(regs.ax, _guest.Get(DS.galacticCursorY));
  _guest.Set(DS.chartCursorY, Low(regs.ax));
  _guest.Set(DS.chartIsShortRange, 0);
  do
  {
    DrawGalacticChart(_guest);
    _guest.Call(PRESENT_CHART_FRAME);
    MoveChartCursor(_guest);
  } while (!ReadChartKey(_guest, GALACTIC_CHART_KEYS));
}

void ShowShortRangeChart(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.sunFringeMask, 0);
  _guest.Call(DRAW_CHART_FRAME);
  regs.si = DS.shortRangeChartTitle.offset;
  regs.di = CHART_TITLE;
  _guest.Set(DS.textPaperPattern, 0);
  regs.bx = INK_1;
  _guest.Call(DRAW_SCREEN_STRING);
  _guest.Call(CLEAR_CHART_TEXT_LINES);
  SetLow(regs.ax, _guest.Get(DS.shortRangeCursorX));
  _guest.Set(DS.chartCursorX, Low(regs.ax));
  SetLow(regs.ax, _guest.Get(DS.shortRangeCursorY));
  _guest.Set(DS.chartCursorY, Low(regs.ax));
  _guest.Set(DS.chartIsShortRange, 1);
  _guest.Set(DS.chartItemCount, 0);
  regs.ax = DS.chartItems.offset;
  _guest.Set(DS.chartItemEnd, regs.ax);
  regs.ax = DS.pendingChartLabels.offset;
  _guest.Set(DS.chartLabelCursor, regs.ax);
  _guest.Call(LOAD_GALAXY_SEEDS);
  SetLow(regs.ax, 0);
  for (;;)
  {
    _guest.Push(regs.ax);
    _guest.Call(GET_SHORT_RANGE_OFFSET);
    if (_guest.Flag(Machine::FLAG_CARRY))
    {
      AddShortRangeSystem(_guest);
    }
    else
    {
      _guest.Call(ADVANCE_TO_NEXT_SYSTEM);
      _guest.JumpBack(SHORT_RANGE_SYSTEM_DONE);
    }
    regs.ax = _guest.Pop();
    SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) + 1));
    if (Low(regs.ax) == 0)
    {
      break;
    }
    _guest.JumpBack(SHORT_RANGE_NEXT_SYSTEM);
  }
  _guest.Call(PLACE_CHART_LABELS);
  do
  {
    DrawShortRangeChart(_guest);
    _guest.Call(PRESENT_CHART_FRAME);
    MoveChartCursor(_guest);
  } while (!ReadChartKey(_guest, SHORT_RANGE_CHART_KEYS));
}

void GetShortRangeOffset(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Get(DS.chartIsShortRange) == 0)
  {
    _guest.SetFlag(Machine::FLAG_CARRY, true);
    return;
  }
  regs.dx = static_cast<std::uint16_t>(_guest.Get(DS.systemX) - _guest.Get(DS.currentSystemX));
  regs.ax = Magnitude(regs.dx);
  if (regs.ax >= SHORT_RANGE_HALF_WIDTH)
  {
    _guest.SetFlag(Machine::FLAG_CARRY, false);
    return;
  }
  regs.cx = static_cast<std::uint16_t>((_guest.Get(DS.systemY) >> 1) - _guest.Get(DS.currentSystemChartY));
  regs.ax = Magnitude(regs.cx);
  _guest.SetFlag(Machine::FLAG_CARRY, regs.ax < SHORT_RANGE_HALF_HEIGHT);
}

void IsSystemOnChart(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Get(DS.chartIsShortRange) == 0)
  {
    _guest.SetFlag(Machine::FLAG_CARRY, true);
    return;
  }
  const std::uint16_t ax = regs.ax;
  const std::uint16_t cx = regs.cx;
  const std::uint16_t dx = regs.dx;
  GetShortRangeOffset(_guest);
  regs.dx = dx;
  regs.cx = cx;
  regs.ax = ax;
}

void TwistSystemSeeds(Guest& _guest)
{
  const std::uint16_t a = _guest.Get(DS.systemSeed0);
  const std::uint16_t b = _guest.Get(DS.systemSeed1);
  const std::uint16_t c = _guest.Get(DS.systemSeed2);
  _guest.Set(DS.systemSeed0, b);
  _guest.Set(DS.systemSeed1, c);
  _guest.Set(DS.systemSeed2, static_cast<std::uint16_t>(a + b + c));
}

void LoadGalaxySeeds(Guest& _guest)
{
  // The table offset is figured in BL, so it wraps at a byte.
  const auto entry = static_cast<std::uint8_t>(_guest.Get(DS.galaxyNumber) * GALAXY_SEED_BYTES);
  const auto seeds = static_cast<std::uint16_t>(DS.galaxySeeds.offset + entry);
  _guest.Set(DS.systemSeed0, _guest.Word(seeds));
  _guest.Set(DS.systemSeed1, _guest.Word(static_cast<std::uint16_t>(seeds + 2)));
  _guest.Set(DS.systemSeed2, _guest.Word(static_cast<std::uint16_t>(seeds + 4)));
}

void GetCursorGalaxyPosition(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = MakeWord(_guest.Get(DS.chartCursorX), _guest.Get(DS.chartCursorY));
  if (_guest.Get(DS.chartIsShortRange) == 1)
  {
    const std::uint8_t cursorRow = High(regs.bx);
    const auto x = ChartToGalaxy(static_cast<std::uint16_t>(Low(regs.bx) - SHORT_RANGE_CENTER_X), _guest.Get(DS.currentSystemX));
    const auto y = ChartToGalaxy(static_cast<std::uint16_t>(cursorRow - SHORT_RANGE_CENTER_ROW), _guest.Get(DS.currentSystemChartY));
    regs.cx = MakeWord(Low(x), SHORT_RANGE_DIVISOR);
    regs.bx = MakeWord(Low(x), Low(y));
  }
  regs.ax = regs.bx;
}

void MoveCursorToSystem(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  if (_guest.Get(DS.chartIsShortRange) != 1)
  {
    _guest.Set(DS.chartCursorX, _guest.Get(DS.systemX));
    SetLow(regs.ax, static_cast<std::uint8_t>(_guest.Get(DS.systemY) >> 1));
    _guest.Set(DS.chartCursorY, Low(regs.ax));
    return;
  }
  regs.bx = static_cast<std::uint16_t>(_guest.Get(DS.systemX) - _guest.Get(DS.currentSystemX));
  _guest.Set(DS.chartCursorX, Low(GalaxyToChart(regs.bx, SHORT_RANGE_CENTER_X)));
  regs.bx = static_cast<std::uint16_t>((_guest.Get(DS.systemY) >> 1) - _guest.Get(DS.currentSystemChartY));
  regs.ax = GalaxyToChart(regs.bx, SHORT_RANGE_CENTER_ROW);
  regs.bx = static_cast<std::uint16_t>(static_cast<std::int16_t>(regs.bx) >> 1);
  _guest.Set(DS.chartCursorY, Low(regs.ax));
}

void SelectSystemAtCursor(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  FindNearestSystem(_guest);
  ComputeDistanceToSystem(_guest);
  regs.ax = _guest.Get(DS.selectedDistanceTenthsLy);
  regs.di = DS.distanceDigits.offset;
  _guest.Call(FORMAT_DECIMAL_5);
  regs.di = DS.distanceDigits.offset;
  regs.cx = 3;
  _guest.Call(BLANK_LEADING_ZEROS);

  const std::uint8_t seed1 = Low(_guest.Get(DS.systemSeed1));
  const std::uint8_t seed2 = Low(_guest.Get(DS.systemSeed2));
  const std::uint8_t seed2High = _guest.Get(DS.systemSeed2High);
  const std::uint8_t x = _guest.Get(DS.systemX);
  const std::uint8_t y = _guest.Get(DS.systemY);

  const auto government = static_cast<std::uint8_t>((seed1 >> 3) & 7);
  _guest.Set(DS.selectedGovernment, government);
  // Governments 0 and 1 set bit 1 of the economy before it is inverted.
  const auto economy = static_cast<std::uint8_t>((((government >> 1) == 0 ? 2 : 0) | (y & 7)) ^ 7);
  _guest.Set(DS.selectedEconomy, economy);
  const auto techLevel = static_cast<std::uint8_t>(((economy + 3) & x) + (seed2 & 1));
  _guest.Set(DS.selectedTechLevel, techLevel);
  const auto population =
    static_cast<std::uint8_t>((Low(static_cast<std::uint16_t>(techLevel * economy)) >> 1) + 0x14 + ((seed1 >> 3) & 7));
  _guest.Set(DS.selectedPopulationTenthsBillion, population);

  if ((seed2 & 0x80) == 0)
  {
    _guest.Set(DS.selectedSpeciesAdjective1, 0xFF);
  }
  else
  {
    const auto mixed = static_cast<std::uint8_t>((x ^ y) & 7);
    _guest.Set(DS.selectedSpeciesAdjective1, static_cast<std::uint8_t>((seed2High >> 2) & 7));
    _guest.Set(DS.selectedSpeciesAdjective2, static_cast<std::uint8_t>(seed2High >> 5));
    _guest.Set(DS.selectedSpeciesAdjective3, mixed);
    _guest.Set(DS.selectedSpeciesType, static_cast<std::uint8_t>(((seed2High & 3) + mixed) & 7));
  }

  // (government+8)^2 in AL, times the population, times 4.
  const std::uint8_t squared = Low(Square(static_cast<std::uint8_t>(government + 8)));
  _guest.Set(DS.selectedProductivityMillionCredits, static_cast<std::uint16_t>((squared * population) << 2));

  const std::uint16_t rows = MakeWord(y, y);
  const auto rotated = static_cast<std::uint16_t>((rows << 2) | (rows >> 14));
  const std::uint16_t seed2Word = _guest.Get(DS.systemSeed2);
  const auto swapped = static_cast<std::uint16_t>(((seed2Word >> 8) | (seed2Word << 8)) & 0x3FF);
  _guest.Set(DS.selectedRadiusKm, static_cast<std::uint16_t>(((rotated ^ swapped) & 0xFFF) + 0x10E1));

  const auto descriptionSeed0 = static_cast<std::uint16_t>(_guest.Get(DS.systemSeed0) ^ _guest.Get(DS.systemSeed1));
  _guest.Set(DS.descriptionSeed0, descriptionSeed0);
  _guest.Set(DS.descriptionSeed1, static_cast<std::uint16_t>(descriptionSeed0 ^ _guest.Get(DS.systemSeed2)));
  GenerateSystemName(_guest);
}

void FindNearestSystem(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  LoadGalaxySeeds(_guest);
  GetCursorGalaxyPosition(_guest);
  const std::uint16_t cursor = regs.bx;
  regs.si = 0xFFFF;
  for (regs.cx = GALAXY_SYSTEMS; regs.cx != 0; --regs.cx)
  {
    const std::uint16_t count = regs.cx;
    // dx^2 + (dy/2)^2 to the cursor, each square a byte multiply.
    regs.cx = MakeWord(Low(count), High(cursor));
    regs.bx = Low(cursor);
    regs.ax = Square(Low(Magnitude(static_cast<std::uint16_t>(_guest.Get(DS.systemX) - regs.bx))));
    regs.dx = regs.ax;
    regs.bx = High(cursor);
    regs.ax = Square(Low(Magnitude(static_cast<std::uint16_t>((_guest.Get(DS.systemY) >> 1) - regs.bx))));
    const std::uint32_t distance = std::uint32_t{regs.ax} + regs.dx;
    regs.ax = static_cast<std::uint16_t>(distance);
    if (distance <= 0xFFFF && regs.ax < regs.si)
    {
      IsSystemOnChart(_guest);
      if (Carry(regs))
      {
        regs.si = regs.ax;
        regs.cx = Low(regs.cx);
        regs.bp = regs.cx;
      }
    }
    AdvanceToNextSystem(_guest);
    regs.bx = cursor;
    regs.cx = count;
  }
  regs.cx = static_cast<std::uint16_t>(GALAXY_SYSTEMS - regs.bp);
  _guest.Set(DS.selectedSystemIndex, Low(regs.cx));
  LoadSystemSeeds(_guest);
  MoveCursorToSystem(_guest);
}

void ComputeDistanceToSystem(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = _guest.Get(DS.currentSystemX);
  regs.ax = Square(Low(Magnitude(static_cast<std::uint16_t>(_guest.Get(DS.systemX) - regs.bx))));
  regs.dx = regs.ax;
  regs.bx = _guest.Get(DS.currentSystemChartY);
  regs.ax = Square(Low(Magnitude(static_cast<std::uint16_t>((_guest.Get(DS.systemY) >> 1) - regs.bx))));
  // The square root of the 17-bit sum in BX:AX, counting the odd numbers it takes away.
  std::int32_t remaining = std::int32_t{regs.ax} + regs.dx;
  regs.dx = 0xFFFF;
  regs.cx = regs.dx;
  do
  {
    ++regs.cx;
    regs.dx = static_cast<std::uint16_t>(regs.dx + 2);
    remaining -= regs.dx;
  } while (remaining >= 0);
  regs.bx = 0xFFFF;
  regs.ax = static_cast<std::uint16_t>(regs.cx << 2);
  _guest.Set(DS.selectedDistanceTenthsLy, regs.ax);
}

void ShowNearestSystemDistance(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  FindNearestSystem(_guest);
  ComputeDistanceToSystem(_guest);
  regs.ax = _guest.Get(DS.selectedDistanceTenthsLy);
  regs.di = DS.distanceDigits.offset;
  _guest.Call(FORMAT_DECIMAL_5);
  regs.di = DS.distanceDigits.offset;
  regs.cx = 3;
  _guest.Call(BLANK_LEADING_ZEROS);
  regs.si = DS.distanceText.offset;
  constexpr std::array<std::uint16_t, 4> PLACES = {DISTANCE_TEXT_HUNDREDS, DISTANCE_TEXT_TENS, DISTANCE_TEXT_UNITS, DISTANCE_TEXT_TENTHS};
  constexpr std::array<DataField<std::uint8_t>, 4> DIGITS = {DS.distanceHundredsDigit, DS.distanceTensDigit, DS.distanceUnitsDigit,
                                                             DS.distanceTenthsDigit};
  for (std::size_t digit = 0; digit < DIGITS.size(); ++digit)
  {
    SetLow(regs.ax, _guest.Get(DIGITS[digit]));
    _guest.SetByte(Offset(regs.si, PLACES[digit]), Low(regs.ax));
  }
  _guest.Set(DS.textPaperPattern, 0);
  regs.bx = INK_3;
  regs.di = CHART_TEXT_LINE_2;
  _guest.Call(DRAW_SCREEN_STRING);
  GenerateSystemName(_guest);
  regs.si = DS.selectedSystemName.offset;
  regs.di = CHART_TEXT_LINE_1;
  regs.bx = INK_2;
  _guest.Call(DRAW_SCREEN_STRING);
  regs.si = DS.nameLinePadding.offset;
  regs.di = CHART_NAME_PADDING;
  _guest.Call(DRAW_SCREEN_STRING);
}

void LoadSystemSeeds(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.cx = Low(regs.cx);
  LoadGalaxySeeds(_guest);
  for (; regs.cx != 0; --regs.cx)
  {
    AdvanceToNextSystem(_guest);
  }
}

void AdvanceToNextSystem(Guest& _guest)
{
  TwistSystemSeeds(_guest);
  TwistSystemSeeds(_guest);
  TwistSystemSeeds(_guest);
  TwistSystemSeeds(_guest);
}

void GenerateSystemName(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.data75CA, 0x2020);
  _guest.Set(DS.data75C8, 0x2020);
  regs.dx = Low(_guest.Get(DS.systemSeed0));
  regs.si = DS.selectedSystemName.offset;
  for (regs.cx = 4; regs.cx != 0; --regs.cx)
  {
    const std::uint8_t pick = _guest.Get(DS.systemSeed2High);
    TwistSystemSeeds(_guest);
    regs.ax = static_cast<std::uint16_t>((pick & 0x1F) << 1);
    regs.bx = static_cast<std::uint16_t>(DS.systemNameDigrams.offset + regs.ax);
    regs.ax = _guest.Word(regs.bx);
    if (regs.cx == 1)
    {
      // The fourth pair only when bit 6 of systemSeed0 was set.
      SetLow(regs.dx, static_cast<std::uint8_t>(Low(regs.dx) & 0x40));
      if (Low(regs.dx) == 0)
      {
        continue;
      }
    }
    _guest.SetWord(regs.si, regs.ax);
    if (High(regs.ax) != SPACE)
    {
      ++regs.si;
      SetHigh(regs.dx, static_cast<std::uint8_t>(High(regs.dx) + 1));
    }
    if (Low(regs.ax) != SPACE)
    {
      ++regs.si;
      SetHigh(regs.dx, static_cast<std::uint8_t>(High(regs.dx) + 1));
    }
  }
  _guest.Set(DS.selectedSystemNameLength, High(regs.dx));
}

void FindSystemByName(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Call(LOAD_GALAXY_SEEDS);
  regs.si = DS.findPrompt.offset;
  regs.di = CHART_TEXT_LINE_1;
  regs.bx = INK_1;
  _guest.Call(DRAW_SCREEN_STRING);
  regs.si = DS.blankChartLine.offset;
  regs.di = CHART_TEXT_LINE_2;
  _guest.Call(DRAW_SCREEN_STRING);
  regs.si = DS.findInput.offset;
  regs.ax = 0;
  for (std::uint16_t word = 0; word < SYSTEM_NAME_BYTES; word = static_cast<std::uint16_t>(word + 2))
  {
    _guest.SetWord(Offset(regs.si, word), regs.ax);
  }
  regs.di = CHART_TEXT_LINE_2;
  SetLow(regs.cx, static_cast<std::uint8_t>(SYSTEM_NAME_BYTES));
  _guest.Call(READ_TEXT_LINE);
  if (_guest.Byte(DS.findInput.offset) == 0)
  {
    // Nothing typed: on into ShowNearestSystemDistance, by a jump back to its entry.
    _guest.JumpBack(SHOW_NEAREST_SYSTEM_DISTANCE);
    _guest.Call(SHOW_NEAREST_SYSTEM_DISTANCE);
    return;
  }

  // The name echoed for the error message and upper-cased in place, a space after it; CX its length,
  // counting the space when there is room for it.
  regs.si = DS.findInput.offset;
  regs.di = DS.findInputEcho.offset;
  regs.cx = 0;
  for (;;)
  {
    SetLow(regs.ax, _guest.Byte(regs.si));
    _guest.SetByte(regs.di, Low(regs.ax));
    if (Low(regs.ax) == 0)
    {
      break;
    }
    SetLow(regs.cx, static_cast<std::uint8_t>(Low(regs.cx) + 1));
    _guest.SetByte(regs.si, static_cast<std::uint8_t>(_guest.Byte(regs.si) & UPPER_CASE_MASK));
    ++regs.si;
    ++regs.di;
    _guest.JumpBack(FIND_UPPER_CASE);
  }
  _guest.SetByte(regs.si, SPACE);
  if (regs.cx != SYSTEM_NAME_BYTES)
  {
    ++regs.cx;
  }

  // Each system's name in turn, compared with it.
  regs.ax = regs.ds;
  regs.es = regs.ax;
  regs.ax = 0;
  for (;;)
  {
    _guest.Push(regs.ax);
    _guest.Push(regs.cx);
    _guest.Call(GENERATE_SYSTEM_NAME);
    regs.cx = _guest.Pop();
    _guest.Push(regs.cx);
    regs.si = DS.selectedSystemName.offset;
    regs.di = DS.findInput.offset;
    const bool found = CompareBytes(_guest);
    regs.cx = _guest.Pop();
    regs.ax = _guest.Pop();
    if (found)
    {
      SetLow(regs.cx, Low(regs.ax));
      _guest.Push(regs.cx);
      _guest.Call(LOAD_GALAXY_SEEDS);
      regs.cx = _guest.Pop();
      regs.ax = Guest::VIDEO_SEGMENT;
      regs.es = regs.ax;
      _guest.Call(LOAD_SYSTEM_SEEDS);
      _guest.Call(GET_SHORT_RANGE_OFFSET);
      if (!_guest.Flag(Machine::FLAG_CARRY))
      {
        _guest.JumpBack(FIND_NOT_ON_MAP);
        ShowNotOnMap(_guest);
        return;
      }
      _guest.Call(MOVE_CURSOR_TO_SYSTEM);
      _guest.Call(SHOW_NEAREST_SYSTEM_DISTANCE);
      return;
    }
    SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) + 1));
    if (Low(regs.ax) == 0)
    {
      break;
    }
    _guest.JumpBack(FIND_NEXT_SYSTEM);
  }
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
  ShowNotOnMap(_guest);
}

void DrawChartItems(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.cx = _guest.Get(DS.chartItemCount);
  if (regs.cx == 0)
  {
    return;
  }
  _guest.Set(DS.drawColor, CHART_DISC_COLOR);
  regs.di = DS.chartItems.offset;
  do
  {
    const std::uint16_t count = regs.cx;
    const std::uint16_t item = regs.di;
    regs.bx = _guest.Word(static_cast<std::uint16_t>(item + 6));
    if (High(regs.bx) != 0)
    {
      // A label, its text at its top-left corner.
      regs.si = _guest.Word(static_cast<std::uint16_t>(item + 4));
      regs.dx = MakeWord(_guest.Byte(item), _guest.Byte(static_cast<std::uint16_t>(item + 2)));
      regs.di = regs.dx;
      regs.bx = 0xFFFF;
      _guest.Call(DRAW_SMALL_VIEW_STRING);
    }
    else
    {
      regs.dx = _guest.Word(static_cast<std::uint16_t>(item + 4));
      SetLow(regs.cx, High(regs.dx));
      SetHigh(regs.dx, 0);
      _guest.Call(DRAW_DISC);
    }
    regs.di = static_cast<std::uint16_t>(item + CHART_ITEM_BYTES);
    regs.cx = count;
  } while (--regs.cx != 0);
}

void PlaceChartLabels(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  SetLow(regs.ax, _guest.Get(DS.chartItemCount));
  if (Low(regs.ax) == 0)
  {
    return;
  }
  _guest.Set(DS.labelsLeftToPlace, Low(regs.ax));
  regs.di = DS.pendingChartLabels.offset;
  _guest.Set(DS.chartLabelCursor, regs.di);
  do
  {
    regs.di = _guest.Get(DS.chartLabelCursor);
    regs.dx = _guest.Word(regs.di);
    regs.bx = _guest.Word(static_cast<std::uint16_t>(regs.di + 2));
    regs.di = static_cast<std::uint16_t>(regs.di + 4);
    _guest.Set(DS.chartLabelCursor, regs.di);
    _guest.Set(DS.labelNudgeCount, 0);
    while (AnyChartItemOverlaps(_guest))
    {
      NudgeChartLabel(_guest);
      if (!Carry(regs))
      {
        break;
      }
    }
    AddChartLabel(_guest);
    _guest.Set(DS.labelsLeftToPlace, static_cast<std::uint8_t>(_guest.Get(DS.labelsLeftToPlace) - 1));
  } while (_guest.Get(DS.labelsLeftToPlace) != 0);
}

void AddChartLabel(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = _guest.Get(DS.chartItemEnd);
  _guest.SetWord(regs.di, regs.dx);
  _guest.SetWord(static_cast<std::uint16_t>(regs.di + 2), regs.bx);
  regs.bx = _guest.Get(DS.chartLabelCursor);
  _guest.SetWord(static_cast<std::uint16_t>(regs.di + 4), regs.bx);
  _guest.SetByte(static_cast<std::uint16_t>(regs.di + 7), 1);
  regs.di = static_cast<std::uint16_t>(regs.di + CHART_ITEM_BYTES);
  _guest.Set(DS.chartItemEnd, regs.di);
  _guest.Set(DS.chartItemCount, static_cast<std::uint8_t>(_guest.Get(DS.chartItemCount) + 1));
  // Past the name's NUL; the scan starts at its second byte.
  do
  {
    ++regs.bx;
  } while (_guest.Byte(regs.bx) != 0);
  ++regs.bx;
  _guest.Set(DS.chartLabelCursor, regs.bx);
}

void NudgeChartLabel(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  for (;;)
  {
    const auto step = static_cast<std::uint8_t>(_guest.Get(DS.labelNudgeCount) + 1);
    _guest.Set(DS.labelNudgeCount, step);
    if (step >= LABEL_TRIES)
    {
      _guest.SetFlag(Machine::FLAG_CARRY, false);
      return;
    }
    if ((step & 1) != 0)
    {
      // Odd steps go up; a move off the top (BL negative) is skipped.
      SetHigh(regs.bx, static_cast<std::uint8_t>(High(regs.bx) - step));
      SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) - step));
      if ((Low(regs.bx) & 0x80) == 0)
      {
        _guest.SetFlag(Machine::FLAG_CARRY, true);
        return;
      }
    }
    else
    {
      SetLow(regs.bx, static_cast<std::uint8_t>(Low(regs.bx) + step));
      SetHigh(regs.bx, static_cast<std::uint8_t>(High(regs.bx) + step));
      if (High(regs.bx) < LABEL_LOWEST_ROW)
      {
        _guest.SetFlag(Machine::FLAG_CARRY, true);
        return;
      }
    }
  }
}

void ChartItemOverlaps(Guest& _guest)
{
  const Machine::Registers& regs = _guest.Regs();
  const std::uint16_t item = regs.di;
  const bool overlaps = _guest.Byte(static_cast<std::uint16_t>(item + 1)) >= Low(regs.dx) && High(regs.dx) >= _guest.Byte(item) &&
                        _guest.Byte(static_cast<std::uint16_t>(item + 3)) >= Low(regs.bx) &&
                        High(regs.bx) >= _guest.Byte(static_cast<std::uint16_t>(item + 2));
  _guest.SetFlag(Machine::FLAG_CARRY, overlaps);
}

void ClearChartTextLines(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = DS.blankChartLine.offset;
  regs.di = CHART_TEXT_LINE_1;
  _guest.Call(DRAW_SCREEN_STRING);
  regs.si = DS.blankChartLine.offset;
  regs.di = CHART_TEXT_LINE_2;
  _guest.Call(DRAW_SCREEN_STRING);
}

void ShowSystemDataScreen(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = DS.systemDataFrame.offset;
  _guest.Call(DRAW_DOCKED_FRAME);
  regs.di = DATA_TITLE;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  _guest.Call(TERMINATE_SELECTED_SYSTEM_NAME);
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = DISTANCE_LABEL;
  regs.di = DISTANCE_ROW;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  _guest.Call(FORMAT_SELECTED_SYSTEM_DISTANCE);
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = LIGHT_YEARS_LABEL;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = ECONOMY_LABEL;
  regs.di = ECONOMY_ROW;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  PrintNamed(_guest, DS.economyNames.offset, static_cast<std::uint16_t>(_guest.Get(DS.selectedEconomy) << 1));
  regs.si = GOVERNMENT_LABEL;
  regs.di = GOVERNMENT_ROW;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  PrintNamed(_guest, DS.governmentNames.offset, static_cast<std::uint16_t>(_guest.Get(DS.selectedGovernment) << 1));
  regs.si = TECH_LEVEL_LABEL;
  regs.di = TECH_LEVEL_ROW;
  _guest.Call(PRINT_TEXT_MODE_STRING);

  // The tech level, one to sixteen, in the two characters at data7D48, a column left when it is one digit.
  SetLow(regs.ax, static_cast<std::uint8_t>(_guest.Get(DS.selectedTechLevel) + 1));
  regs.di = static_cast<std::uint16_t>(regs.di - 2);
  _guest.Set(DS.data7D48, SPACE);
  if (Low(regs.ax) >= TECH_LEVEL_TENS)
  {
    SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) - TECH_LEVEL_TENS));
    _guest.Set(DS.data7D48, '1');
    regs.di = static_cast<std::uint16_t>(regs.di + 2);
  }
  SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) + '0'));
  _guest.Set(DS.data7D49, Low(regs.ax));
  regs.si = DS.data7D48.offset;
  _guest.Call(PRINT_TEXT_MODE_STRING);

  // The population in tenths of a billion, its last two digits about a point.
  regs.ax = _guest.Get(DS.selectedPopulationTenthsBillion);
  regs.di = POPULATION_DIGITS;
  _guest.Call(FORMAT_DECIMAL_5);
  regs.si = POPULATION_LABEL;
  regs.di = POPULATION_ROW;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = POPULATION_SHOWN;
  SetLow(regs.ax, _guest.Byte(Offset(regs.si, 1)));
  _guest.SetByte(regs.si, Low(regs.ax));
  _guest.SetByte(Offset(regs.si, 1), DECIMAL_POINT);
  _guest.Call(PRINT_TEXT_MODE_STRING);

  regs.si = SPECIES_OPEN;
  regs.di = SPECIES_ROW;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  const std::uint8_t size = _guest.Get(DS.selectedSpeciesAdjective1);
  if (size == NO_SPECIES)
  {
    SetLow(regs.ax, 0);
    regs.si = HUMAN_COLONIALS;
    _guest.Call(PRINT_TEXT_MODE_STRING);
  }
  else
  {
    // shl al,1 on each byte, then xor ah,ah.
    PrintNamed(_guest, DS.speciesSizeNames.offset, static_cast<std::uint8_t>(size << 1));
    PrintNamed(_guest, DS.speciesColorNames.offset, static_cast<std::uint8_t>(_guest.Get(DS.selectedSpeciesAdjective2) << 1));
    PrintNamed(_guest, DS.speciesTraitNames.offset, static_cast<std::uint8_t>(_guest.Get(DS.selectedSpeciesAdjective3) << 1));
    PrintNamed(_guest, DS.speciesTypeNames.offset, static_cast<std::uint8_t>(_guest.Get(DS.selectedSpeciesType) << 1));
  }

  // The productivity, its leading zero blanked a column left; the radius, its leading digit blanked.
  regs.ax = _guest.Get(DS.selectedProductivityMillionCredits);
  regs.di = DS.data7EA3.offset;
  _guest.Call(FORMAT_DECIMAL_5);
  regs.si = PRODUCTIVITY_LABEL;
  regs.di = PRODUCTIVITY_ROW;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.si = DS.data7EA3.offset;
  if (_guest.Get(DS.data7EA3) == '0')
  {
    _guest.Set(DS.data7EA3, SPACE);
    regs.di = static_cast<std::uint16_t>(regs.di - 2);
  }
  _guest.Call(PRINT_TEXT_MODE_STRING);
  regs.ax = _guest.Get(DS.selectedRadiusKm);
  regs.di = DS.data7EBB.offset;
  _guest.Call(FORMAT_DECIMAL_5);
  _guest.Set(DS.data7EBB, SPACE);
  regs.si = RADIUS_LABEL;
  regs.di = RADIUS_ROW;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  _guest.Call(SHOW_SYSTEM_DESCRIPTION);
  SetLow(regs.dx, SCAN_F7);
  WaitForScreenExitKey(_guest);
}

void TerminateSelectedSystemName(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.si = DS.selectedSystemName.offset;
  regs.bx = _guest.Get(DS.selectedSystemNameLength);
  _guest.SetByte(static_cast<std::uint16_t>(regs.bx + regs.si), 0);
}

void FormatSelectedSystemDistance(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = DS.distanceTenthsDigit.offset;
  regs.si = SELECTED_DISTANCE_TEXT_END;
  SetLow(regs.ax, _guest.Byte(regs.bx));
  _guest.SetByte(regs.si, Low(regs.ax));
  --regs.si;
  _guest.SetByte(regs.si, DECIMAL_POINT);
  // Backwards through the digits, up to and including the space before them.
  do
  {
    --regs.si;
    --regs.bx;
    SetLow(regs.ax, _guest.Byte(regs.bx));
    _guest.SetByte(regs.si, Low(regs.ax));
  } while (Low(regs.ax) != SPACE);
  ++regs.si;
}

void ShowSystemDescription(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = regs.ds;
  regs.es = regs.ax;
  regs.ax = 0;
  regs.di = DS.descriptionBuffer.offset;
  for (regs.cx = DESCRIPTION_BUFFER_WORDS; regs.cx != 0; --regs.cx)
  {
    _guest.SetFarWord(regs.es, regs.di, regs.ax);
    regs.di = static_cast<std::uint16_t>(regs.di + WordStep(regs));
  }
  regs.di = DS.descriptionBuffer.offset;
  regs.si = DS.descriptionTemplate.offset;
  ExpandDescriptionText(_guest);
  _guest.SetByte(regs.di, 0);

  // Word-wrapped at the last space within 36 characters, a text row at a time.
  regs.si = DS.descriptionBuffer.offset;
  regs.di = DESCRIPTION_SCREEN_OFFSET;
  regs.ax = Guest::VIDEO_SEGMENT;
  regs.es = regs.ax;
  for (;;)
  {
    regs.ax = MakeWord(0, SPACE);
    const std::uint16_t line = regs.si;
    const std::uint16_t cell = regs.di;
    regs.si = static_cast<std::uint16_t>(regs.si + DESCRIPTION_LINE_CHARACTERS);
    if (_guest.Byte(regs.si) == 0)
    {
      regs.si = line;
      regs.di = cell;
      _guest.Call(PRINT_TEXT_MODE_STRING);
      return;
    }
    while (_guest.Byte(regs.si) != SPACE)
    {
      --regs.si;
    }
    _guest.SetByte(regs.si, 0);
    regs.si = line;
    regs.di = cell;
    _guest.Call(PRINT_TEXT_MODE_STRING);
    ++regs.si;
    regs.di = static_cast<std::uint16_t>(cell + TEXT_ROW_BYTES);
  }
}

void ExpandDescriptionText(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  for (;;)
  {
    SetLow(regs.ax, _guest.Byte(regs.si));
    ++regs.si;
    const std::uint8_t code = Low(regs.ax);
    if (code == 0)
    {
      return;
    }
    if (code < SPACE)
    {
      regs.ax = static_cast<std::uint16_t>((code - 1) << 1);
      regs.bx = static_cast<std::uint16_t>(DS.textControlCodes.offset + regs.ax);
      regs.ax = RESUME_TEXT_EXPANSION;
      RunTextControlCode(_guest, _guest.Word(regs.bx));
      continue;
    }
    if (code >= DESCRIPTION_PHRASE_CODE)
    {
      // One of the five phrases in the code's list, by the next random number's low byte / 52.
      regs.ax = static_cast<std::uint16_t>((code - DESCRIPTION_PHRASE_CODE) << 1);
      regs.bx = _guest.Word(static_cast<std::uint16_t>(DS.descriptionPhraseLists.offset + regs.ax));
      NextDescriptionRandom(_guest);
      SetLow(regs.cx, DESCRIPTION_PHRASE_DIVISOR);
      regs.ax = static_cast<std::uint16_t>((Low(regs.ax) / DESCRIPTION_PHRASE_DIVISOR) << 1);
      regs.bx = static_cast<std::uint16_t>(regs.bx + regs.ax);
      const std::uint16_t text = regs.si;
      regs.si = _guest.Word(regs.bx);
      ExpandDescriptionText(_guest);
      regs.si = text;
      continue;
    }
    const std::uint8_t previous = _guest.Byte(static_cast<std::uint16_t>(regs.di - 1));
    if (code == SPACE && previous == SPACE)
    {
      continue;
    }
    // Capitalizing reaches only 0x60-0x78: y and z stay lower case.
    if (_guest.Get(DS.descriptionCapitalize) == 1 && previous == SPACE && code >= 0x60 && code < 0x79)
    {
      SetLow(regs.ax, static_cast<std::uint8_t>(code & 0xDF));
    }
    _guest.SetByte(regs.di, Low(regs.ax));
    ++regs.di;
  }
}

void InsertSystemName(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t text = regs.si;
  const std::uint16_t output = regs.di;
  CopySelectedNameLower(_guest);
  regs.ax = SPACE;
  _guest.SetWord(regs.di, regs.ax);
  regs.si = DS.descriptionNameBuffer.offset;
  regs.di = output;
  ExpandDescriptionText(_guest);
  regs.si = text;
}

void InsertSystemAdjective(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t text = regs.si;
  const std::uint16_t output = regs.di;
  CopySelectedNameLower(_guest);
  SetLow(regs.ax, _guest.Byte(static_cast<std::uint16_t>(regs.di - 1)));
  const std::uint8_t last = Low(regs.ax);
  if (last == 'a' || last == 'e' || last == 'i' || last == 'o' || last == 'u')
  {
    --regs.di;
  }
  regs.si = DS.adjectiveSuffix.offset;
  regs.cx = ADJECTIVE_SUFFIX_BYTES;
  CopyBytes(_guest);
  regs.si = DS.descriptionNameBuffer.offset;
  regs.di = output;
  ExpandDescriptionText(_guest);
  regs.si = text;
}

void InsertRandomName(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t text = regs.si;
  const std::uint16_t output = regs.di;
  regs.si = DS.selectedSystemName.offset;
  regs.di = SAVED_SYSTEM_NAME;
  regs.cx = SYSTEM_NAME_BYTES;
  CopyBytes(_guest);
  regs.ax = _guest.Get(DS.descriptionSeed0);
  _guest.Set(DS.systemSeed0, regs.ax);
  regs.ax = _guest.Get(DS.descriptionSeed1);
  _guest.Set(DS.systemSeed1, regs.ax);
  regs.ax = static_cast<std::uint16_t>(regs.ax ^ _guest.Get(DS.descriptionSeed0));
  _guest.Set(DS.systemSeed2, regs.ax);
  GenerateSystemName(_guest);
  CopySelectedNameLower(_guest);
  regs.si = SAVED_SYSTEM_NAME;
  regs.di = DS.selectedSystemName.offset;
  regs.cx = SYSTEM_NAME_BYTES;
  CopyBytes(_guest);
  regs.di = output;
  regs.si = DS.descriptionNameBuffer.offset;
  ExpandDescriptionText(_guest);
  regs.si = text;
}

void BackspaceDescription(Guest& _guest)
{
  --_guest.Regs().di;
}

void StartCapitalizing(Guest& _guest)
{
  _guest.Set(DS.descriptionCapitalize, 1);
}

void StopCapitalizing(Guest& _guest)
{
  _guest.Set(DS.descriptionCapitalize, 0);
}

void NextDescriptionRandom(Guest& _guest)
{
  const std::uint16_t a = _guest.Get(DS.descriptionSeed0);
  const std::uint16_t b = _guest.Get(DS.descriptionSeed1);
  const auto sum = static_cast<std::uint16_t>(a + b);
  _guest.Set(DS.descriptionSeed0, b);
  _guest.Set(DS.descriptionSeed1, sum);
  _guest.Regs().ax = sum;
}

void CopySelectedNameLower(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  TerminateSelectedSystemName(_guest);
  regs.si = DS.selectedSystemName.offset;
  regs.di = DS.descriptionNameBuffer.offset;
  SetLow(regs.ax, _guest.Byte(regs.si));
  for (;;)
  {
    _guest.SetByte(regs.di, Low(regs.ax));
    ++regs.si;
    ++regs.di;
    SetLow(regs.ax, _guest.Byte(regs.si));
    if (Low(regs.ax) == 0)
    {
      break;
    }
    SetLow(regs.ax, static_cast<std::uint8_t>(Low(regs.ax) | 0x20));
  }
  _guest.SetByte(regs.di, SPACE);
  _guest.SetByte(static_cast<std::uint16_t>(regs.di + 1), Low(regs.ax));
}

namespace
{

using Machine::FLAG_CARRY;
using Machine::NativeReturn;
using Machine::NativeWait;
using Machine::REGISTER_ALL;
using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DX;
using Machine::REGISTER_ES;
using Machine::REGISTER_SI;

constexpr auto GENERAL =
  static_cast<std::uint16_t>(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP);

// GenerateSystemName clobbers DL but returns DH: DX is compared whole, and the port leaves DL as the
// original does.
// The charts and the data screen wait for keys, and FindSystemByName for a line typed: each waits as a rule.
constexpr std::array ENTRIES = {
  NativeEntry{0x0CAE, "ShowGalacticChart", &ShowGalacticChart, PRESERVES_ALL, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x0E52, "ShowShortRangeChart", &ShowShortRangeChart, PRESERVES_ALL, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x1076, "GetShortRangeOffset", &GetShortRangeOffset, Machine::NativeContract{REGISTER_AX, FLAG_CARRY}},
  NativeEntry{0x10AF, "IsSystemOnChart", &IsSystemOnChart, Machine::NativeContract{0, FLAG_CARRY}},
  NativeEntry{0x10C0, "TwistSystemSeeds", &TwistSystemSeeds, PRESERVES_ALL},
  NativeEntry{0x10D6, "LoadGalaxySeeds", &LoadGalaxySeeds, PRESERVES_ALL},
  NativeEntry{0x10FE, "GetCursorGalaxyPosition", &GetCursorGalaxyPosition, Machine::NativeContract{REGISTER_CX, 0}},
  NativeEntry{0x1146, "MoveCursorToSystem", &MoveCursorToSystem, Machine::NativeContract{REGISTER_AX | REGISTER_BX, 0}},
  NativeEntry{0x1199, "SelectSystemAtCursor", &SelectSystemAtCursor, Machine::NativeContract{GENERAL, 0}},
  NativeEntry{0x1292, "FindNearestSystem", &FindNearestSystem,
              Machine::NativeContract{static_cast<std::uint16_t>(GENERAL & ~REGISTER_DI), 0}},
  NativeEntry{0x12F9, "ComputeDistanceToSystem", &ComputeDistanceToSystem,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, 0}},
  NativeEntry{0x1341, "ShowNearestSystemDistance", &ShowNearestSystemDistance, Machine::NativeContract{GENERAL, 0}},
  NativeEntry{0x139C, "LoadSystemSeeds", &LoadSystemSeeds, PRESERVES_ALL},
  NativeEntry{0x13B4, "AdvanceToNextSystem", &AdvanceToNextSystem, PRESERVES_ALL},
  NativeEntry{0x13C1, "GenerateSystemName", &GenerateSystemName,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI, 0}},
  NativeEntry{0x140D, "FindSystemByName", &FindSystemByName, Machine::NativeContract{GENERAL | REGISTER_ES, 0}, NativeReturn::Near, 0,
              NativeWait::Always},
  NativeEntry{0x14C5, "DrawChartItems", &DrawChartItems, Machine::NativeContract{GENERAL, 0}},
  NativeEntry{0x1505, "PlaceChartLabels", &PlaceChartLabels,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI, 0}},
  NativeEntry{0x1552, "AddChartLabel", &AddChartLabel, Machine::NativeContract{REGISTER_BX | REGISTER_DI, 0}},
  NativeEntry{0x157D, "NudgeChartLabel", &NudgeChartLabel, Machine::NativeContract{0, FLAG_CARRY}},
  NativeEntry{0x15A9, "ChartItemOverlaps", &ChartItemOverlaps, Machine::NativeContract{0, FLAG_CARRY}},
  NativeEntry{0x15BC, "ClearChartTextLines", &ClearChartTextLines, Machine::NativeContract{REGISTER_SI | REGISTER_DI, 0}},
  NativeEntry{0x5CDE, "ShowSystemDataScreen", &ShowSystemDataScreen, PRESERVES_ALL, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x60EB, "TerminateSelectedSystemName", &TerminateSelectedSystemName, Machine::NativeContract{REGISTER_BX, 0}},
  NativeEntry{0x60F7, "FormatSelectedSystemDistance", &FormatSelectedSystemDistance, Machine::NativeContract{REGISTER_AX | REGISTER_BX, 0}},
  NativeEntry{0x6FC0, "ShowSystemDescription", &ShowSystemDescription, Machine::NativeContract{REGISTER_ALL, 0}},
  NativeEntry{0x700F, "ExpandDescriptionText", &ExpandDescriptionText, Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX, 0}},
  NativeEntry{0x707A, "InsertSystemName", &InsertSystemName, PRESERVES_ALL},
  NativeEntry{0x708D, "InsertSystemAdjective", &InsertSystemAdjective, PRESERVES_ALL},
  NativeEntry{0x70C1, "InsertRandomName", &InsertRandomName, PRESERVES_ALL},
  NativeEntry{0x7107, "BackspaceDescription", &BackspaceDescription, PRESERVES_ALL},
  NativeEntry{0x7109, "StartCapitalizing", &StartCapitalizing, PRESERVES_ALL},
  NativeEntry{0x710F, "StopCapitalizing", &StopCapitalizing, PRESERVES_ALL},
  NativeEntry{0x7115, "NextDescriptionRandom", &NextDescriptionRandom, PRESERVES_ALL},
  NativeEntry{0x7124, "CopySelectedNameLower", &CopySelectedNameLower, Machine::NativeContract{REGISTER_AX | REGISTER_SI, 0}},
};

} // namespace

std::span<const NativeEntry> GalaxyEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
