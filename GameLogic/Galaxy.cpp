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

// The control codes' handlers as textControlCodes holds them.
constexpr std::uint16_t INSERT_SYSTEM_NAME = 0x707A;
constexpr std::uint16_t INSERT_SYSTEM_ADJECTIVE = 0x708D;
constexpr std::uint16_t INSERT_RANDOM_NAME = 0x70C1;
constexpr std::uint16_t BACKSPACE_DESCRIPTION = 0x7107;
constexpr std::uint16_t START_CAPITALIZING = 0x7109;
constexpr std::uint16_t STOP_CAPITALIZING = 0x710F;

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
// A chart item, a disc or a label: its x range and its rows, each a byte for the first and one for the last,
// then a label's text or a disc's centre, and a disc's radius or a label's mark in the last byte.
constexpr std::uint8_t CHART_ITEM_BYTES = 8;
constexpr std::uint16_t CHART_ITEM_X = 0;
constexpr std::uint16_t CHART_ITEM_ROWS = 2;
constexpr std::uint16_t CHART_ITEM_TEXT = 4;
constexpr std::uint16_t CHART_ITEM_IS_LABEL = 7;
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
constexpr std::uint8_t LOWER_CASE_BIT = 0x20;

// A system's name: up to four pairs of letters from systemNameDigrams, the fourth only when this bit of systemSeed0 is set.
constexpr std::uint16_t SYSTEM_NAME_PAIRS = 4;
constexpr std::uint16_t SYSTEM_NAME_FOURTH_PAIR = 0x40;

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
[[nodiscard]] std::uint16_t WordStep(bool _backward) noexcept
{
  return _backward ? static_cast<std::uint16_t>(0xFFFE) : static_cast<std::uint16_t>(2);
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

[[nodiscard]] constexpr std::uint16_t Word(std::int16_t _value) noexcept
{
  return static_cast<std::uint16_t>(_value);
}

// A chart item's x range or rows as DX and BX hold them: the first in the low byte, the last in the high.
[[nodiscard]] constexpr ChartSpan SpanOf(std::uint16_t _word) noexcept
{
  return ChartSpan{Low(_word), High(_word)};
}

[[nodiscard]] constexpr std::uint16_t Word(ChartSpan _span) noexcept
{
  return Join(_span.last, _span.first);
}

// The two bytes of a span at DS:_offset, the first below the last.
[[nodiscard]] ChartSpan SpanAt(const GameState& _state, std::uint16_t _offset)
{
  return ChartSpan{_state.Byte(_offset), _state.Byte(Offset(_offset, 1))};
}

// The loop of byte moves (mov al,[si]; mov [di],al; inc si; inc di; loop) some routines make: _count bytes
// from DS:_from to DS:_to, 65,536 of them for a count of 0. Returns the last byte moved, which the loop leaves
// in AL; it leaves SI and DI _count bytes on, and CX at 0.
std::uint8_t CopyBytes(GameState& _state, std::uint16_t _from, std::uint16_t _to, std::uint16_t _count)
{
  std::uint8_t moved = 0;
  for (std::uint32_t done = 0; done < LoopCount(_count); ++done)
  {
    moved = _state.Byte(static_cast<std::uint16_t>(_from + done));
    _state.SetByte(static_cast<std::uint16_t>(_to + done), moved);
  }
  return moved;
}

// One pass of PlaceChartLabels over chartItems (CS:152B): whether an item overlaps the label with x range _x and
// rows _rows. It stops at the first that does, and LOOP counts chartItemCount items, 65,536 for 0.
[[nodiscard]] bool AnyChartItemOverlaps(const GameState& _state, ChartSpan _x, ChartSpan _rows)
{
  std::uint16_t item = DS.chartItems.offset;
  std::uint16_t itemsLeft = _state.Get(DS.chartItemCount);
  do
  {
    if (ChartItemOverlaps(_state, item, _x, _rows))
    {
      return true;
    }
    item = Offset(item, CHART_ITEM_BYTES);
  } while (--itemsLeft != 0);
  return false;
}

// A control code's handler, as textControlCodes holds it, and its value routine.
struct TextControlHandler
{
  std::uint16_t offset;
  DescriptionOutput (*run)(GameState&, std::uint16_t); // with the output's place
};

constexpr std::array<TextControlHandler, 6> TEXT_CONTROL_HANDLERS = {{
  {INSERT_SYSTEM_NAME, &InsertSystemName},
  {INSERT_SYSTEM_ADJECTIVE, &InsertSystemAdjective},
  {INSERT_RANDOM_NAME, &InsertRandomName},
  {BACKSPACE_DESCRIPTION, [](GameState&, std::uint16_t _output) { return DescriptionOutput{BackspaceDescription(_output), std::nullopt}; }},
  {START_CAPITALIZING,
   [](GameState& _state, std::uint16_t _output)
   {
     StartCapitalizing(_state);
     return DescriptionOutput{_output, std::nullopt};
   }},
  {STOP_CAPITALIZING,
   [](GameState& _state, std::uint16_t _output)
   {
     StopCapitalizing(_state);
     return DescriptionOutput{_output, std::nullopt};
   }},
}};

// jmp [bx] to the control code handler at CS:_handler, with SI and the return to ResumeTextExpansion pushed; the
// handler returns there, which pops SI. The stack keeps no more than SI and the way back, so the handler's value
// routine runs on the output at DS:_output. Codes 7-31 jump into descriptionPhraseLists, whose words the original
// would run as code, and so would a code whose handler a description had written over; the routine runs nothing for
// them. No text in the game holds such a code, and no description reaches textControlCodes: the longest is 144 bytes
// of descriptionBuffer's 256.
DescriptionOutput RunTextControlCode(GameState& _state, std::uint16_t _handler, std::uint16_t _output)
{
  for (const TextControlHandler& handler : TEXT_CONTROL_HANDLERS)
  {
    if (handler.offset == _handler)
    {
      return handler.run(_state, _output);
    }
  }
  return DescriptionOutput{_output, std::nullopt};
}

// What the expansion of _inner leaves of _outer: the inner one's place, and the last random name made in either.
void ContinueOutput(DescriptionOutput& _outer, const DescriptionOutput& _inner)
{
  _outer.next = _inner.next;
  if (_inner.lastRandomName)
  {
    _outer.lastRandomName = _inner.lastRandomName;
  }
}

// The pair of letters GenerateSystemName reads last from _seeds, whether or not it writes it: its pick is systemSeed2's
// high byte after three twists.
[[nodiscard]] std::uint16_t LastNamePair(const GameState& _state, SystemSeeds _seeds)
{
  SystemSeeds seeds = _seeds;
  for (std::uint16_t pair = 1; pair < SYSTEM_NAME_PAIRS; ++pair)
  {
    seeds = SystemSeeds{seeds.seed1, seeds.seed2, static_cast<std::uint16_t>(seeds.seed0 + seeds.seed1 + seeds.seed2)};
  }
  return _state.Word(Offset(DS.systemNameDigrams.offset, static_cast<std::uint16_t>((High(seeds.seed2) & 0x1F) << 1)));
}

// What GenerateSystemName leaves in DX for _name: the length in DH, and in DL bit 6 of the seed it began from.
[[nodiscard]] std::uint16_t RandomNameDx(RandomName _name) noexcept
{
  return Join(_name.length, _name.fourthPair);
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
  void (*recenter)(GameState&);
  bool recenterThroughAl; // the recentring goes through AL, and leaves the cursor's row there
  DataField<std::uint8_t> keptCursorX;
  DataField<std::uint8_t> keptCursorY;
};

// The galactic chart's cursor back on the current system (CS:0E19).
void RecenterGalacticCursor(GameState& _state)
{
  _state.Set(DS.chartCursorX, _state.Get(DS.currentSystemX));
  _state.Set(DS.chartCursorY, _state.Get(DS.currentSystemChartY));
}

// The short-range chart's cursor back on its centre (CS:103F).
void RecenterShortRangeCursor(GameState& _state)
{
  _state.Set(DS.chartCursorX, static_cast<std::uint8_t>(SHORT_RANGE_CENTER_X));
  _state.Set(DS.chartCursorY, static_cast<std::uint8_t>(SHORT_RANGE_CENTER_ROW));
}

constexpr ChartKeys GALACTIC_CHART_KEYS{.frame = GALACTIC_CHART_FRAME,
                                        .ignored = GALACTIC_KEY_IGNORED,
                                        .ownKey = SCAN_F5,
                                        .recenter = &RecenterGalacticCursor,
                                        .recenterThroughAl = true,
                                        .keptCursorX = DS.galacticCursorX,
                                        .keptCursorY = DS.galacticCursorY};
constexpr ChartKeys SHORT_RANGE_CHART_KEYS{.frame = SHORT_RANGE_CHART_FRAME,
                                           .ignored = SHORT_RANGE_KEY_IGNORED,
                                           .ownKey = SCAN_F6,
                                           .recenter = &RecenterShortRangeCursor,
                                           .recenterThroughAl = false,
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
    _chart.recenter(_guest.State());
    if (_chart.recenterThroughAl)
    {
      SetLow(regs.ax, _guest.Get(DS.chartCursorY));
    }
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

// What REPE CMPSB finds.
struct ByteComparison
{
  std::uint16_t compared; // the bytes it compared, which SI and DI move on by and CX goes down by
  bool equal;             // every byte compared was equal: ZF
  bool below;             // the last byte compared was below the other: CF
};

// REPE CMPSB: DS:_source with _segment:_destination, for at most _count bytes, backwards when _backwards, until
// two differ. A count of 0 compares nothing, and the flags keep what they held.
[[nodiscard]] ByteComparison CompareBytes(const GameState& _state, std::uint16_t _source, std::uint16_t _segment,
                                          std::uint16_t _destination, std::uint16_t _count, bool _backwards)
{
  const auto step = static_cast<std::uint16_t>(_backwards ? 0xFFFF : 1);
  ByteComparison comparison{0, true, false};
  std::uint16_t source = _source;
  std::uint16_t destination = _destination;
  while (comparison.compared < _count && comparison.equal)
  {
    const std::uint8_t sourceByte = _state.Byte(source);
    const std::uint8_t destinationByte = _state.FarByte(_segment, destination);
    comparison.equal = sourceByte == destinationByte;
    comparison.below = sourceByte < destinationByte;
    source = Offset(source, step);
    destination = Offset(destination, step);
    ++comparison.compared;
  }
  return comparison;
}

// What ShowNotOnMap draws: its three parts, the name as typed between the other two.
struct NotOnMapMessage
{
  PrintedText prefix;
  PrintedText name;
  PrintedText suffix;
};

// FindSystemByName's 'ERROR: <name> not on map!' on the second line under the chart (CS:148B), at _segment.
NotOnMapMessage ShowNotOnMap(GameState& _state, std::uint16_t _segment)
{
  NotOnMapMessage message{};
  message.prefix = DrawScreenString(_state, DS.findErrorPrefix.offset, INK_3, _segment, CHART_TEXT_LINE_2);
  message.name = DrawScreenString(_state, DS.findInputEcho.offset, INK_2, _segment, message.prefix.nextCell);
  message.suffix = DrawScreenString(_state, DS.findErrorSuffix.offset, INK_3, _segment, message.name.nextCell);
  return message;
}

// ShowNotOnMap at ES, and the registers as the original leaves them: DrawScreenString's SI, DI and AX for the
// suffix, and BX its ink.
void ShowNotOnMapOnRegisters(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const NotOnMapMessage message = ShowNotOnMap(_guest.State(), regs.es);
  regs.ax = DrawnScreenStringAx(_guest.State(), DS.findErrorPrefix.offset, message.prefix, INK_3, regs.ax);
  regs.ax = DrawnScreenStringAx(_guest.State(), DS.findInputEcho.offset, message.name, INK_2, regs.ax);
  regs.ax = DrawnScreenStringAx(_guest.State(), DS.findErrorSuffix.offset, message.suffix, INK_3, regs.ax);
  regs.si = message.suffix.end;
  regs.di = message.suffix.nextCell;
  regs.bx = INK_3;
}

// PrintTextModeString, at B800:_cell, of the text whose pointer is _bytes into the table at DS:_table.
PrintedText PrintNamed(GameState& _state, std::uint16_t _table, std::uint16_t _bytes, std::uint16_t _cell)
{
  return PrintTextModeString(_state, _state.Word(Offset(_table, _bytes)), _cell);
}

// PrintNamed from DI, and the registers as the original leaves them: BX the pointer's place, and PrintTextModeString's
// SI, DI, ES and AX.
void PrintNamedOnRegisters(Guest& _guest, std::uint16_t _table, std::uint16_t _bytes)
{
  Machine::Registers& regs = _guest.Regs();
  const PrintedText printed = PrintNamed(_guest.State(), _table, _bytes, regs.di);
  regs.bx = Offset(_table, _bytes);
  regs.si = printed.end;
  regs.di = printed.nextCell;
  regs.es = Guest::VIDEO_SEGMENT;
  regs.ax = Join(_guest.Get(DS.textAttribute), 0);
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
    _guest.Spend(GALACTIC_CHART_PACING); // the IBM PC's redraw (D18)
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
    _guest.Spend(SHORT_RANGE_CHART_PACING); // the IBM PC's redraw (D18)
    _guest.Call(PRESENT_CHART_FRAME);
    MoveChartCursor(_guest);
  } while (!ReadChartKey(_guest, SHORT_RANGE_CHART_KEYS));
}

SystemSeeds ReadSystemSeeds(const GameState& _state)
{
  return SystemSeeds{_state.Get(DS.systemSeed0), _state.Get(DS.systemSeed1), _state.Get(DS.systemSeed2)};
}

ShortRangeOffset GetShortRangeOffset(const GameState& _state)
{
  if (_state.Get(DS.chartIsShortRange) == 0)
  {
    return ShortRangeOffset{.onChart = true, .x = std::nullopt, .row = std::nullopt};
  }
  const SystemSeeds seeds = ReadSystemSeeds(_state);
  const auto x = static_cast<std::int16_t>(seeds.X() - _state.Get(DS.currentSystemX));
  if (Magnitude(Word(x)) >= SHORT_RANGE_HALF_WIDTH)
  {
    return ShortRangeOffset{.onChart = false, .x = x, .row = std::nullopt};
  }
  const auto row = static_cast<std::int16_t>(seeds.Row() - _state.Get(DS.currentSystemChartY));
  return ShortRangeOffset{.onChart = Magnitude(Word(row)) < SHORT_RANGE_HALF_HEIGHT, .x = x, .row = row};
}

bool IsSystemOnChart(const GameState& _state)
{
  // CMP chartIsShortRange,1 sets CF on the galactic chart, where GetShortRangeOffset would find every system too.
  return GetShortRangeOffset(_state).onChart;
}

void TwistSystemSeeds(GameState& _state)
{
  const SystemSeeds seeds = ReadSystemSeeds(_state);
  // In the order the original's two XCHGs and its ADD write them.
  _state.Set(DS.systemSeed1, seeds.seed2);
  _state.Set(DS.systemSeed0, seeds.seed1);
  _state.Set(DS.systemSeed2, static_cast<std::uint16_t>(seeds.seed0 + seeds.seed1 + seeds.seed2));
}

void LoadGalaxySeeds(GameState& _state)
{
  // The table offset is figured in BL, so it wraps at a byte.
  const auto entry = static_cast<std::uint8_t>(_state.Get(DS.galaxyNumber) * GALAXY_SEED_BYTES);
  const std::uint16_t seeds = Offset(DS.galaxySeeds.offset, entry);
  _state.Set(DS.systemSeed0, _state.Word(seeds));
  _state.Set(DS.systemSeed1, _state.Word(Offset(seeds, 2)));
  _state.Set(DS.systemSeed2, _state.Word(Offset(seeds, 4)));
}

ChartPoint GetCursorGalaxyPosition(const GameState& _state)
{
  const ChartPoint cursor{_state.Get(DS.chartCursorX), _state.Get(DS.chartCursorY)};
  if (_state.Get(DS.chartIsShortRange) != 1)
  {
    return cursor;
  }
  const std::uint16_t x = ChartToGalaxy(static_cast<std::uint16_t>(cursor.x - SHORT_RANGE_CENTER_X), _state.Get(DS.currentSystemX));
  const std::uint16_t row =
    ChartToGalaxy(static_cast<std::uint16_t>(cursor.row - SHORT_RANGE_CENTER_ROW), _state.Get(DS.currentSystemChartY));
  return ChartPoint{Low(x), Low(row)};
}

void MoveCursorToSystem(GameState& _state)
{
  const SystemSeeds seeds = ReadSystemSeeds(_state);
  if (_state.Get(DS.chartIsShortRange) != 1)
  {
    _state.Set(DS.chartCursorX, seeds.X());
    _state.Set(DS.chartCursorY, seeds.Row());
    return;
  }
  const auto across = static_cast<std::uint16_t>(seeds.X() - _state.Get(DS.currentSystemX));
  _state.Set(DS.chartCursorX, Low(GalaxyToChart(across, SHORT_RANGE_CENTER_X)));
  const auto down = static_cast<std::uint16_t>(seeds.Row() - _state.Get(DS.currentSystemChartY));
  _state.Set(DS.chartCursorY, Low(GalaxyToChart(down, SHORT_RANGE_CENTER_ROW)));
}

void SelectSystemAtCursor(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  FindNearestSystemEntry(_guest);
  ComputeDistanceToSystemEntry(_guest);
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
  GenerateSystemNameEntry(_guest);
}

std::uint8_t FindNearestSystem(GameState& _state, std::uint16_t _countIfNone)
{
  LoadGalaxySeeds(_state);
  const ChartPoint cursor = GetCursorGalaxyPosition(_state);
  // SI the nearest distance so far, and BP the loop's count, CL, at the nearest.
  std::uint16_t nearest = 0xFFFF;
  std::uint16_t nearestCount = _countIfNone;
  for (std::uint16_t count = GALAXY_SYSTEMS; count != 0; --count)
  {
    // dx^2 + (dy/2)^2 to the cursor, each square a byte multiply; a sum that carries is passed over.
    const std::uint16_t across = Square(Low(Magnitude(static_cast<std::uint16_t>(_state.Get(DS.systemX) - cursor.x))));
    const std::uint16_t down = Square(Low(Magnitude(static_cast<std::uint16_t>((_state.Get(DS.systemY) >> 1) - cursor.row))));
    const std::uint32_t distance = std::uint32_t{across} + down;
    if (distance <= 0xFFFF && distance < nearest && IsSystemOnChart(_state))
    {
      nearest = static_cast<std::uint16_t>(distance);
      nearestCount = Low(count);
    }
    AdvanceToNextSystem(_state);
  }
  const auto index = static_cast<std::uint8_t>(GALAXY_SYSTEMS - nearestCount);
  _state.Set(DS.selectedSystemIndex, index);
  LoadSystemSeeds(_state, index);
  MoveCursorToSystem(_state);
  return index;
}

std::uint16_t ComputeDistanceToSystem(GameState& _state)
{
  const SystemSeeds seeds = ReadSystemSeeds(_state);
  // Each difference made positive, and its low byte squared by a byte multiply.
  const std::uint16_t across = Square(Low(Magnitude(static_cast<std::uint16_t>(seeds.X() - _state.Get(DS.currentSystemX)))));
  const std::uint16_t down = Square(Low(Magnitude(static_cast<std::uint16_t>(seeds.Row() - _state.Get(DS.currentSystemChartY)))));
  // The square root of their 17-bit sum, counting the odd numbers it takes away.
  std::int32_t remaining = std::int32_t{across} + down;
  std::uint16_t odd = 0xFFFF;
  std::uint16_t root = 0xFFFF;
  do
  {
    ++root;
    odd = static_cast<std::uint16_t>(odd + 2);
    remaining -= odd;
  } while (remaining >= 0);
  const auto distanceTenthsLy = static_cast<std::uint16_t>(root << 2);
  _state.Set(DS.selectedDistanceTenthsLy, distanceTenthsLy);
  return distanceTenthsLy;
}

void ShowNearestSystemDistance(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  FindNearestSystemEntry(_guest);
  ComputeDistanceToSystemEntry(_guest);
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
  GenerateSystemNameEntry(_guest);
  regs.si = DS.selectedSystemName.offset;
  regs.di = CHART_TEXT_LINE_1;
  regs.bx = INK_2;
  _guest.Call(DRAW_SCREEN_STRING);
  regs.si = DS.nameLinePadding.offset;
  regs.di = CHART_NAME_PADDING;
  _guest.Call(DRAW_SCREEN_STRING);
}

void LoadSystemSeeds(GameState& _state, std::uint8_t _system)
{
  LoadGalaxySeeds(_state);
  // Four TwistSystemSeeds a system, as AdvanceToNextSystem makes them.
  for (std::uint8_t left = _system; left != 0; --left)
  {
    AdvanceToNextSystem(_state);
  }
}

void AdvanceToNextSystem(GameState& _state)
{
  TwistSystemSeeds(_state);
  TwistSystemSeeds(_state);
  TwistSystemSeeds(_state);
  TwistSystemSeeds(_state);
}

std::uint8_t GenerateSystemName(GameState& _state)
{
  _state.Set(DS.data75CA, 0x2020);
  _state.Set(DS.data75C8, 0x2020);
  // The fourth pair only when bit 6 of systemSeed0 was set.
  const bool fourPairs = (_state.Get(DS.systemSeed0) & SYSTEM_NAME_FOURTH_PAIR) != 0;
  std::uint16_t name = DS.selectedSystemName.offset;
  std::uint8_t length = 0;
  for (std::uint16_t pair = 0; pair < SYSTEM_NAME_PAIRS; ++pair)
  {
    const std::uint8_t pick = _state.Get(DS.systemSeed2High);
    TwistSystemSeeds(_state);
    const std::uint16_t digram = _state.Word(Offset(DS.systemNameDigrams.offset, static_cast<std::uint16_t>((pick & 0x1F) << 1)));
    if (pair + 1 == SYSTEM_NAME_PAIRS && !fourPairs)
    {
      continue;
    }
    // A word: a letter of the pair that is a space is written, and the next pair overwrites it.
    _state.SetWord(name, digram);
    if (High(digram) != SPACE)
    {
      name = Offset(name, 1);
      ++length;
    }
    if (Low(digram) != SPACE)
    {
      name = Offset(name, 1);
      ++length;
    }
  }
  _state.Set(DS.selectedSystemNameLength, length);
  return length;
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
    // REPE CMPSB, as many bytes as CX says.
    bool found = _guest.Flag(Machine::FLAG_ZERO);
    if (regs.cx != 0)
    {
      const bool backwards = _guest.Flag(Machine::FLAG_DIRECTION);
      const ByteComparison comparison = CompareBytes(_guest.State(), regs.si, regs.es, regs.di, regs.cx, backwards);
      const auto moved = static_cast<std::uint16_t>(backwards ? 0u - comparison.compared : comparison.compared);
      regs.si = Offset(regs.si, moved);
      regs.di = Offset(regs.di, moved);
      regs.cx = static_cast<std::uint16_t>(regs.cx - comparison.compared);
      _guest.SetFlag(Machine::FLAG_ZERO, comparison.equal);
      _guest.SetFlag(Machine::FLAG_CARRY, comparison.below);
      found = comparison.equal;
    }
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
        ShowNotOnMapOnRegisters(_guest);
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
  ShowNotOnMapOnRegisters(_guest);
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

void PlaceChartLabels(GameState& _state)
{
  const std::uint8_t labels = _state.Get(DS.chartItemCount);
  if (labels == 0)
  {
    return;
  }
  _state.Set(DS.labelsLeftToPlace, labels);
  _state.Set(DS.chartLabelCursor, DS.pendingChartLabels.offset);
  do
  {
    // The label's x range and rows, then its name.
    const std::uint16_t label = _state.Get(DS.chartLabelCursor);
    const ChartSpan x = SpanOf(_state.Word(label));
    ChartSpan rows = SpanOf(_state.Word(Offset(label, 2)));
    _state.Set(DS.chartLabelCursor, Offset(label, 4));
    _state.Set(DS.labelNudgeCount, 0);
    while (AnyChartItemOverlaps(_state, x, rows))
    {
      const LabelNudge nudge = NudgeChartLabel(_state, rows);
      rows = nudge.rows;
      if (!nudge.moved)
      {
        break;
      }
    }
    AddChartLabel(_state, x, rows);
    _state.Set(DS.labelsLeftToPlace, static_cast<std::uint8_t>(_state.Get(DS.labelsLeftToPlace) - 1));
  } while (_state.Get(DS.labelsLeftToPlace) != 0);
}

void AddChartLabel(GameState& _state, ChartSpan _x, ChartSpan _rows)
{
  const std::uint16_t item = _state.Get(DS.chartItemEnd);
  _state.SetWord(Offset(item, CHART_ITEM_X), Word(_x));
  _state.SetWord(Offset(item, CHART_ITEM_ROWS), Word(_rows));
  const std::uint16_t name = _state.Get(DS.chartLabelCursor);
  _state.SetWord(Offset(item, CHART_ITEM_TEXT), name);
  _state.SetByte(Offset(item, CHART_ITEM_IS_LABEL), 1);
  _state.Set(DS.chartItemEnd, Offset(item, CHART_ITEM_BYTES));
  _state.Set(DS.chartItemCount, static_cast<std::uint8_t>(_state.Get(DS.chartItemCount) + 1));
  // Past the name's NUL; the scan starts at its second byte.
  std::uint16_t past = name;
  do
  {
    past = Offset(past, 1);
  } while (_state.Byte(past) != 0);
  _state.Set(DS.chartLabelCursor, Offset(past, 1));
}

LabelNudge NudgeChartLabel(GameState& _state, ChartSpan _rows)
{
  ChartSpan rows = _rows;
  for (;;)
  {
    const auto step = static_cast<std::uint8_t>(_state.Get(DS.labelNudgeCount) + 1);
    _state.Set(DS.labelNudgeCount, step);
    if (step >= LABEL_TRIES)
    {
      return LabelNudge{.moved = false, .rows = rows};
    }
    if ((step & 1) != 0)
    {
      // Odd steps go up; a move off the top (the first row negative) is skipped.
      rows.last = static_cast<std::uint8_t>(rows.last - step);
      rows.first = static_cast<std::uint8_t>(rows.first - step);
      if ((rows.first & 0x80) == 0)
      {
        return LabelNudge{.moved = true, .rows = rows};
      }
    }
    else
    {
      rows.first = static_cast<std::uint8_t>(rows.first + step);
      rows.last = static_cast<std::uint8_t>(rows.last + step);
      if (rows.last < LABEL_LOWEST_ROW)
      {
        return LabelNudge{.moved = true, .rows = rows};
      }
    }
  }
}

bool ChartItemOverlaps(const GameState& _state, std::uint16_t _item, ChartSpan _x, ChartSpan _rows)
{
  const ChartSpan itemX = SpanAt(_state, Offset(_item, CHART_ITEM_X));
  const ChartSpan itemRows = SpanAt(_state, Offset(_item, CHART_ITEM_ROWS));
  return itemX.last >= _x.first && _x.last >= itemX.first && itemRows.last >= _rows.first && _rows.last >= itemRows.first;
}

PrintedText ClearChartTextLines(GameState& _state, std::uint16_t _ink, std::uint16_t _segment)
{
  DrawScreenString(_state, DS.blankChartLine.offset, _ink, _segment, CHART_TEXT_LINE_1);
  return DrawScreenString(_state, DS.blankChartLine.offset, _ink, _segment, CHART_TEXT_LINE_2);
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
  PrintNamedOnRegisters(_guest, DS.economyNames.offset, static_cast<std::uint16_t>(_guest.Get(DS.selectedEconomy) << 1));
  regs.si = GOVERNMENT_LABEL;
  regs.di = GOVERNMENT_ROW;
  _guest.Call(PRINT_TEXT_MODE_STRING);
  PrintNamedOnRegisters(_guest, DS.governmentNames.offset, static_cast<std::uint16_t>(_guest.Get(DS.selectedGovernment) << 1));
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
    PrintNamedOnRegisters(_guest, DS.speciesSizeNames.offset, static_cast<std::uint8_t>(size << 1));
    PrintNamedOnRegisters(_guest, DS.speciesColorNames.offset, static_cast<std::uint8_t>(_guest.Get(DS.selectedSpeciesAdjective2) << 1));
    PrintNamedOnRegisters(_guest, DS.speciesTraitNames.offset, static_cast<std::uint8_t>(_guest.Get(DS.selectedSpeciesAdjective3) << 1));
    PrintNamedOnRegisters(_guest, DS.speciesTypeNames.offset, static_cast<std::uint8_t>(_guest.Get(DS.selectedSpeciesType) << 1));
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

std::uint8_t TerminateSelectedSystemName(GameState& _state)
{
  const std::uint8_t length = _state.Get(DS.selectedSystemNameLength);
  _state.SetByte(Offset(DS.selectedSystemName.offset, length), 0);
  return length;
}

std::uint16_t FormatSelectedSystemDistance(GameState& _state)
{
  std::uint16_t from = DS.distanceTenthsDigit.offset;
  std::uint16_t to = SELECTED_DISTANCE_TEXT_END;
  _state.SetByte(to, _state.Byte(from));
  --to;
  _state.SetByte(to, DECIMAL_POINT);
  // Backwards through the digits, up to and including the space before them.
  std::uint8_t copied = 0;
  do
  {
    --to;
    --from;
    copied = _state.Byte(from);
    _state.SetByte(to, copied);
  } while (copied != SPACE);
  return Offset(to, 1);
}

void ShowSystemDescription(GameState& _state, bool _backward)
{
  // REP STOSW of zeros over descriptionBuffer, ES = DS, forwards or, _backward, down.
  std::uint16_t word = DS.descriptionBuffer.offset;
  for (std::uint16_t words = DESCRIPTION_BUFFER_WORDS; words != 0; --words)
  {
    _state.SetWord(word, 0);
    word = Offset(word, WordStep(_backward));
  }
  const ExpandedText expanded = ExpandDescriptionText(_state, DS.descriptionTemplate.offset, DS.descriptionBuffer.offset);
  _state.SetByte(expanded.output.next, 0);

  // Word-wrapped at the last space within 36 characters, a text row at a time.
  std::uint16_t line = DS.descriptionBuffer.offset;
  std::uint16_t cell = DESCRIPTION_SCREEN_OFFSET;
  for (;;)
  {
    std::uint16_t end = Offset(line, DESCRIPTION_LINE_CHARACTERS);
    if (_state.Byte(end) == 0)
    {
      (void)PrintTextModeString(_state, line, cell);
      return;
    }
    while (_state.Byte(end) != SPACE)
    {
      --end;
    }
    _state.SetByte(end, 0);
    line = Offset(PrintTextModeString(_state, line, cell).end, 1);
    cell = Offset(cell, TEXT_ROW_BYTES);
  }
}

ExpandedText ExpandDescriptionText(GameState& _state, std::uint16_t _text, std::uint16_t _output)
{
  ExpandedText expanded{_text, DescriptionOutput{_output, std::nullopt}};
  for (;;)
  {
    const std::uint8_t code = _state.Byte(expanded.end);
    expanded.end = Offset(expanded.end, 1);
    if (code == 0)
    {
      return expanded;
    }
    if (code < SPACE)
    {
      const std::uint16_t handler = _state.Word(DS.textControlCodes.At(static_cast<std::uint8_t>(code - 1)));
      ContinueOutput(expanded.output, RunTextControlCode(_state, handler, expanded.output.next));
      continue;
    }
    if (code >= DESCRIPTION_PHRASE_CODE)
    {
      // One of the five phrases in the code's list, by the next random number's low byte / 52.
      const std::uint16_t list = _state.Word(DS.descriptionPhraseLists.At(static_cast<std::uint8_t>(code - DESCRIPTION_PHRASE_CODE)));
      const auto pick = static_cast<std::uint8_t>(Low(NextDescriptionRandom(_state)) / DESCRIPTION_PHRASE_DIVISOR);
      const std::uint16_t phrase = _state.Word(Offset(list, static_cast<std::uint16_t>(pick << 1)));
      ContinueOutput(expanded.output, ExpandDescriptionText(_state, phrase, expanded.output.next).output);
      continue;
    }
    const std::uint16_t output = expanded.output.next;
    const std::uint8_t previous = _state.Byte(static_cast<std::uint16_t>(output - 1));
    if (code == SPACE && previous == SPACE)
    {
      continue;
    }
    // Capitalizing reaches only 0x60-0x78: y and z stay lower case.
    std::uint8_t character = code;
    if (_state.Get(DS.descriptionCapitalize) == 1 && previous == SPACE && code >= 0x60 && code < 0x79)
    {
      character = static_cast<std::uint8_t>(code & UPPER_CASE_MASK);
    }
    _state.SetByte(output, character);
    expanded.output.next = Offset(output, 1);
  }
}

DescriptionOutput InsertSystemName(GameState& _state, std::uint16_t _output)
{
  // MOV AX,20h / MOV [DI],AX: the space and the NUL after the name again, as a word.
  _state.SetWord(CopySelectedNameLower(_state), SPACE);
  return ExpandDescriptionText(_state, DS.descriptionNameBuffer.offset, _output).output;
}

DescriptionOutput InsertSystemAdjective(GameState& _state, std::uint16_t _output)
{
  std::uint16_t suffix = CopySelectedNameLower(_state);
  const std::uint8_t last = _state.Byte(static_cast<std::uint16_t>(suffix - 1));
  if (last == 'a' || last == 'e' || last == 'i' || last == 'o' || last == 'u')
  {
    --suffix;
  }
  CopyBytes(_state, DS.adjectiveSuffix.offset, suffix, ADJECTIVE_SUFFIX_BYTES);
  return ExpandDescriptionText(_state, DS.descriptionNameBuffer.offset, _output).output;
}

DescriptionOutput InsertRandomName(GameState& _state, std::uint16_t _output)
{
  CopyBytes(_state, DS.selectedSystemName.offset, SAVED_SYSTEM_NAME, SYSTEM_NAME_BYTES);
  const std::uint16_t seed0 = _state.Get(DS.descriptionSeed0);
  _state.Set(DS.systemSeed0, seed0);
  const std::uint16_t seed1 = _state.Get(DS.descriptionSeed1);
  _state.Set(DS.systemSeed1, seed1);
  _state.Set(DS.systemSeed2, static_cast<std::uint16_t>(seed1 ^ _state.Get(DS.descriptionSeed0)));
  const RandomName name{GenerateSystemName(_state), static_cast<std::uint8_t>(seed0 & SYSTEM_NAME_FOURTH_PAIR)};
  CopySelectedNameLower(_state);
  CopyBytes(_state, SAVED_SYSTEM_NAME, DS.selectedSystemName.offset, SYSTEM_NAME_BYTES);
  DescriptionOutput output{_output, name};
  ContinueOutput(output, ExpandDescriptionText(_state, DS.descriptionNameBuffer.offset, _output).output);
  return output;
}

std::uint16_t BackspaceDescription(std::uint16_t _output)
{
  return static_cast<std::uint16_t>(_output - 1);
}

void StartCapitalizing(GameState& _state)
{
  _state.Set(DS.descriptionCapitalize, 1);
}

void StopCapitalizing(GameState& _state)
{
  _state.Set(DS.descriptionCapitalize, 0);
}

std::uint16_t NextDescriptionRandom(GameState& _state)
{
  const std::uint16_t a = _state.Get(DS.descriptionSeed0);
  const std::uint16_t b = _state.Get(DS.descriptionSeed1);
  const auto sum = static_cast<std::uint16_t>(a + b);
  _state.Set(DS.descriptionSeed0, b);
  _state.Set(DS.descriptionSeed1, sum);
  return sum;
}

std::uint16_t CopySelectedNameLower(GameState& _state)
{
  TerminateSelectedSystemName(_state);
  std::uint16_t from = DS.selectedSystemName.offset;
  std::uint16_t to = DS.descriptionNameBuffer.offset;
  std::uint8_t letter = _state.Byte(from);
  for (;;)
  {
    _state.SetByte(to, letter);
    from = Offset(from, 1);
    to = Offset(to, 1);
    letter = _state.Byte(from);
    if (letter == 0)
    {
      break;
    }
    letter = static_cast<std::uint8_t>(letter | LOWER_CASE_BIT);
  }
  // The space, then the NUL from AL.
  _state.SetByte(to, SPACE);
  _state.SetByte(Offset(to, 1), letter);
  return to;
}

// ── Their entries ──

namespace
{

using Machine::FLAG_CARRY;
using Machine::NativeContract;
using Machine::NativeReturn;
using Machine::NativeWait;
using Machine::REGISTER_ALL;
using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DS;
using Machine::REGISTER_DX;
using Machine::REGISTER_ES;
using Machine::REGISTER_SI;

constexpr auto GENERAL =
  static_cast<std::uint16_t>(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP);

constexpr NativeContract CLOBBERS_CX{REGISTER_CX, 0};
constexpr NativeContract CLOBBERS_AX_BX{REGISTER_AX | REGISTER_BX, 0};
constexpr NativeContract CLOBBERS_BX_DI{REGISTER_BX | REGISTER_DI, 0};
constexpr NativeContract CLOBBERS_AX_BX_CX_DX{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};
constexpr NativeContract CLOBBERS_SI{REGISTER_SI, 0};
// GenerateSystemName's: it clobbers DL but returns DH, so DX is compared whole, and the entry leaves DL as the
// original does; and AX, the last pair it read, which InsertRandomName keeps through CopySelectedNameLower.
constexpr NativeContract CLOBBERS_BX_CX_SI{REGISTER_BX | REGISTER_CX | REGISTER_SI, 0};
constexpr NativeContract RETURNS_CARRY{0, FLAG_CARRY};
constexpr NativeContract CLOBBERS_AX_RETURNS_CARRY{REGISTER_AX, FLAG_CARRY};
constexpr NativeContract PLACES_LABELS{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI, 0};
constexpr NativeContract CLEARS_TEXT_LINES{REGISTER_SI | REGISTER_DI, 0};
constexpr NativeContract EXPANDS_TEXT{REGISTER_AX | REGISTER_BX | REGISTER_CX, 0};
constexpr NativeContract FINDS_NEAREST{static_cast<std::uint16_t>(GENERAL & ~REGISTER_DI), 0};
// ShowSystemDescription's: all but DS, which the original leaves alone and ShowSystemDataScreen goes on with.
constexpr NativeContract SHOWS_DESCRIPTION{static_cast<std::uint16_t>(REGISTER_ALL & ~REGISTER_DS), 0};

// What the control codes that expand a name leave of _output: DI past it, DX as the last GenerateSystemName leaves it
// when one ran, and BX the length TerminateSelectedSystemName indexes the name by. A name is letters (systemNameDigrams),
// and expanding it changes no other register but AL, the NUL it ends at, which each entry sets with AH.
void InsertedNameOut(Guest& _guest, const DescriptionOutput& _output)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = _output.next;
  if (_output.lastRandomName)
  {
    regs.dx = RandomNameDx(*_output.lastRandomName);
  }
  regs.bx = _guest.Get(DS.selectedSystemNameLength);
}

} // namespace

void GetShortRangeOffsetEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ShortRangeOffset offset = GetShortRangeOffset(_guest.State());
  // DX and CX keep what they held where the original measures no x or no row.
  if (offset.x)
  {
    regs.dx = Word(*offset.x);
  }
  if (offset.row)
  {
    regs.cx = Word(*offset.row);
  }
  _guest.SetFlag(FLAG_CARRY, offset.onChart);
  _guest.Clobber(CLOBBERS_AX_RETURNS_CARRY);
}

void IsSystemOnChartEntry(Guest& _guest)
{
  _guest.SetFlag(FLAG_CARRY, IsSystemOnChart(_guest.State()));
  _guest.Clobber(RETURNS_CARRY);
}

void TwistSystemSeedsEntry(Guest& _guest)
{
  TwistSystemSeeds(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

void LoadGalaxySeedsEntry(Guest& _guest)
{
  LoadGalaxySeeds(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

void GetCursorGalaxyPositionEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ChartPoint position = GetCursorGalaxyPosition(_guest.State());
  regs.bx = Join(position.row, position.x);
  regs.ax = regs.bx;
  _guest.Clobber(CLOBBERS_CX);
}

void MoveCursorToSystemEntry(Guest& _guest)
{
  MoveCursorToSystem(_guest.State());
  _guest.Clobber(CLOBBERS_AX_BX);
}

void FindNearestSystemEntry(Guest& _guest)
{
  // BP, the count the index is made from when no system is on the chart.
  (void)FindNearestSystem(_guest.State(), _guest.Regs().bp);
  _guest.Clobber(FINDS_NEAREST);
}

void ComputeDistanceToSystemEntry(Guest& _guest)
{
  ComputeDistanceToSystem(_guest.State());
  _guest.Clobber(CLOBBERS_AX_BX_CX_DX);
}

void LoadSystemSeedsEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  LoadSystemSeeds(_guest.State(), Low(regs.cx));
  // CH cleared, and the LOOP counts CX down to 0.
  regs.cx = 0;
  _guest.Clobber(PRESERVES_ALL);
}

void AdvanceToNextSystemEntry(Guest& _guest)
{
  AdvanceToNextSystem(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

void GenerateSystemNameEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // The original reads the fourth pair into AX whether or not it writes it, and InsertRandomName keeps AH: its pick is
  // systemSeed2's high byte after three twists.
  SystemSeeds seeds = ReadSystemSeeds(_guest.State());
  const std::uint16_t seed0 = seeds.seed0;
  for (std::uint16_t pair = 1; pair < SYSTEM_NAME_PAIRS; ++pair)
  {
    seeds = SystemSeeds{seeds.seed1, seeds.seed2, static_cast<std::uint16_t>(seeds.seed0 + seeds.seed1 + seeds.seed2)};
  }
  const auto pick = static_cast<std::uint16_t>((High(seeds.seed2) & 0x1F) << 1);
  const std::uint16_t lastPair = _guest.Word(Offset(DS.systemNameDigrams.offset, pick));
  const std::uint8_t length = GenerateSystemName(_guest.State());
  regs.ax = lastPair;
  // It counts the length in DH, and its test for the fourth pair leaves bit 6 of systemSeed0 in DL.
  regs.dx = Join(length, static_cast<std::uint8_t>(seed0 & SYSTEM_NAME_FOURTH_PAIR));
  _guest.Clobber(CLOBBERS_BX_CX_SI);
}

void PlaceChartLabelsEntry(Guest& _guest)
{
  PlaceChartLabels(_guest.State());
  _guest.Clobber(PLACES_LABELS);
}

void AddChartLabelEntry(Guest& _guest)
{
  const Machine::Registers& regs = _guest.Regs();
  AddChartLabel(_guest.State(), SpanOf(regs.dx), SpanOf(regs.bx));
  _guest.Clobber(CLOBBERS_BX_DI);
}

void NudgeChartLabelEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const LabelNudge nudge = NudgeChartLabel(_guest.State(), SpanOf(regs.bx));
  regs.bx = Word(nudge.rows);
  _guest.SetFlag(FLAG_CARRY, nudge.moved);
  _guest.Clobber(RETURNS_CARRY);
}

void ChartItemOverlapsEntry(Guest& _guest)
{
  const Machine::Registers& regs = _guest.Regs();
  _guest.SetFlag(FLAG_CARRY, ChartItemOverlaps(_guest.State(), regs.di, SpanOf(regs.dx), SpanOf(regs.bx)));
  _guest.Clobber(RETURNS_CARRY);
}

void ClearChartTextLinesEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const PrintedText drawn = ClearChartTextLines(_guest.State(), regs.bx, regs.es);
  // The second DrawScreenString's SI and DI, and its AX, which the contract keeps: the same text drawn twice leaves
  // what the second leaves.
  regs.ax = DrawnScreenStringAx(_guest.State(), DS.blankChartLine.offset, drawn, regs.bx, regs.ax);
  regs.si = drawn.end;
  regs.di = drawn.nextCell;
  _guest.Clobber(CLEARS_TEXT_LINES);
}

void TerminateSelectedSystemNameEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // The original indexes the name by its length in BX, and leaves it there: CopySelectedNameLower keeps BX.
  regs.bx = TerminateSelectedSystemName(_guest.State());
  regs.si = DS.selectedSystemName.offset;
  _guest.Clobber(PRESERVES_ALL);
}

void FormatSelectedSystemDistanceEntry(Guest& _guest)
{
  _guest.Regs().si = FormatSelectedSystemDistance(_guest.State());
  _guest.Clobber(CLOBBERS_AX_BX);
}

void ShowSystemDescriptionEntry(Guest& _guest)
{
  ShowSystemDescription(_guest.State(), _guest.Flag(Machine::FLAG_DIRECTION));
  _guest.Clobber(SHOWS_DESCRIPTION);
}

void ExpandDescriptionTextEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const ExpandedText expanded = ExpandDescriptionText(_guest.State(), regs.si, regs.di);
  regs.si = expanded.end;
  regs.di = expanded.output.next;
  // DX, which the contract keeps, as the last GenerateSystemName leaves it, when control code 3 ran.
  if (expanded.output.lastRandomName)
  {
    regs.dx = RandomNameDx(*expanded.output.lastRandomName);
  }
  _guest.Clobber(EXPANDS_TEXT);
}

void InsertSystemNameEntry(Guest& _guest)
{
  InsertedNameOut(_guest, InsertSystemName(_guest.State(), _guest.Regs().di));
  // MOV AX,20h before the expansion, which ends with the NUL in AL.
  _guest.Regs().ax = 0;
  _guest.Clobber(PRESERVES_ALL);
}

void InsertSystemAdjectiveEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t ah = High(regs.ax);
  InsertedNameOut(_guest, InsertSystemAdjective(_guest.State(), regs.di));
  // AH as it came in, which only AL's loads pass over, and the LOOP that copies the suffix counts CX down to 0.
  regs.ax = Join(ah, 0);
  regs.cx = 0;
  _guest.Clobber(PRESERVES_ALL);
}

void InsertRandomNameEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t seed0 = _guest.Get(DS.descriptionSeed0);
  const std::uint16_t seed1 = _guest.Get(DS.descriptionSeed1);
  const std::uint16_t lastPair = LastNamePair(_guest.State(), SystemSeeds{seed0, seed1, static_cast<std::uint16_t>(seed1 ^ seed0)});
  InsertedNameOut(_guest, InsertRandomName(_guest.State(), regs.di));
  // AH the last pair GenerateSystemName read, which only AL's loads pass over, and the LOOPs that copy the name count CX
  // down to 0.
  regs.ax = Join(High(lastPair), 0);
  regs.cx = 0;
  _guest.Clobber(PRESERVES_ALL);
}

void BackspaceDescriptionEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = BackspaceDescription(regs.di);
  _guest.Clobber(PRESERVES_ALL);
}

void StartCapitalizingEntry(Guest& _guest)
{
  StartCapitalizing(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

void StopCapitalizingEntry(Guest& _guest)
{
  StopCapitalizing(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

void NextDescriptionRandomEntry(Guest& _guest)
{
  _guest.Regs().ax = NextDescriptionRandom(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

void CopySelectedNameLowerEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.di = CopySelectedNameLower(_guest.State());
  // TerminateSelectedSystemName leaves the name's length in BX, which the original keeps, and it reads the NUL into AL
  // and leaves AH alone: InsertSystemAdjective keeps AH.
  regs.bx = _guest.Get(DS.selectedSystemNameLength);
  SetLow(regs.ax, 0);
  _guest.Clobber(CLOBBERS_SI);
}

namespace
{

// The charts and the data screen wait for keys, and FindSystemByName for a line typed: each waits as a rule.
constexpr std::array ENTRIES = {
  NativeEntry{0x0CAE, "ShowGalacticChart", &ShowGalacticChart, PRESERVES_ALL, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x0E52, "ShowShortRangeChart", &ShowShortRangeChart, PRESERVES_ALL, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x1076, "GetShortRangeOffset", &GetShortRangeOffsetEntry, CLOBBERS_AX_RETURNS_CARRY},
  NativeEntry{0x10AF, "IsSystemOnChart", &IsSystemOnChartEntry, RETURNS_CARRY},
  NativeEntry{0x10C0, "TwistSystemSeeds", &TwistSystemSeedsEntry, PRESERVES_ALL},
  NativeEntry{0x10D6, "LoadGalaxySeeds", &LoadGalaxySeedsEntry, PRESERVES_ALL},
  NativeEntry{0x10FE, "GetCursorGalaxyPosition", &GetCursorGalaxyPositionEntry, CLOBBERS_CX},
  NativeEntry{0x1146, "MoveCursorToSystem", &MoveCursorToSystemEntry, CLOBBERS_AX_BX},
  NativeEntry{0x1199, "SelectSystemAtCursor", &SelectSystemAtCursor, Machine::NativeContract{GENERAL, 0}},
  NativeEntry{0x1292, "FindNearestSystem", &FindNearestSystemEntry, FINDS_NEAREST},
  NativeEntry{0x12F9, "ComputeDistanceToSystem", &ComputeDistanceToSystemEntry, CLOBBERS_AX_BX_CX_DX},
  NativeEntry{0x1341, "ShowNearestSystemDistance", &ShowNearestSystemDistance, Machine::NativeContract{GENERAL, 0}},
  NativeEntry{0x139C, "LoadSystemSeeds", &LoadSystemSeedsEntry, PRESERVES_ALL},
  NativeEntry{0x13B4, "AdvanceToNextSystem", &AdvanceToNextSystemEntry, PRESERVES_ALL},
  NativeEntry{0x13C1, "GenerateSystemName", &GenerateSystemNameEntry, CLOBBERS_BX_CX_SI},
  NativeEntry{0x140D, "FindSystemByName", &FindSystemByName, Machine::NativeContract{GENERAL | REGISTER_ES, 0}, NativeReturn::Near, 0,
              NativeWait::Always},
  NativeEntry{0x14C5, "DrawChartItems", &DrawChartItems, Machine::NativeContract{GENERAL, 0}},
  NativeEntry{0x1505, "PlaceChartLabels", &PlaceChartLabelsEntry, PLACES_LABELS},
  NativeEntry{0x1552, "AddChartLabel", &AddChartLabelEntry, CLOBBERS_BX_DI},
  NativeEntry{0x157D, "NudgeChartLabel", &NudgeChartLabelEntry, RETURNS_CARRY},
  NativeEntry{0x15A9, "ChartItemOverlaps", &ChartItemOverlapsEntry, RETURNS_CARRY},
  NativeEntry{0x15BC, "ClearChartTextLines", &ClearChartTextLinesEntry, CLEARS_TEXT_LINES},
  NativeEntry{0x5CDE, "ShowSystemDataScreen", &ShowSystemDataScreen, PRESERVES_ALL, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x60EB, "TerminateSelectedSystemName", &TerminateSelectedSystemNameEntry, PRESERVES_ALL},
  NativeEntry{0x60F7, "FormatSelectedSystemDistance", &FormatSelectedSystemDistanceEntry, CLOBBERS_AX_BX},
  NativeEntry{0x6FC0, "ShowSystemDescription", &ShowSystemDescriptionEntry, SHOWS_DESCRIPTION},
  NativeEntry{0x700F, "ExpandDescriptionText", &ExpandDescriptionTextEntry, EXPANDS_TEXT},
  NativeEntry{0x707A, "InsertSystemName", &InsertSystemNameEntry, PRESERVES_ALL},
  NativeEntry{0x708D, "InsertSystemAdjective", &InsertSystemAdjectiveEntry, PRESERVES_ALL},
  NativeEntry{0x70C1, "InsertRandomName", &InsertRandomNameEntry, PRESERVES_ALL},
  NativeEntry{0x7107, "BackspaceDescription", &BackspaceDescriptionEntry, PRESERVES_ALL},
  NativeEntry{0x7109, "StartCapitalizing", &StartCapitalizingEntry, PRESERVES_ALL},
  NativeEntry{0x710F, "StopCapitalizing", &StopCapitalizingEntry, PRESERVES_ALL},
  NativeEntry{0x7115, "NextDescriptionRandom", &NextDescriptionRandomEntry, PRESERVES_ALL},
  NativeEntry{0x7124, "CopySelectedNameLower", &CopySelectedNameLowerEntry, CLOBBERS_SI},
};

} // namespace

std::span<const NativeEntry> GalaxyEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
