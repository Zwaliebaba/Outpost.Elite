#include "pch.h"

#include "Docked.h"
#include "Galaxy.h"

#include "Arithmetic.h"
#include "GalaxyTables.h"
#include "Input.h"
#include "Video.h"

#include <initializer_list>
#include <optional>

namespace Elite
{

namespace
{

// What PresentChartFrame leaves in BP, CopyChartBufferToScreen's MOV BP,20h, the words of a line it copies: the count a chart
// hands ShowNearestSystemDistance, FindSystemByName and SelectSystemAtCursor for when no system is on the chart.
constexpr std::uint16_t PRESENT_CHART_FRAME_BP = 0x20;

// Where the loops of the routines that wait jump back to.
constexpr std::uint16_t SHOW_NEAREST_SYSTEM_DISTANCE = 0x1341; // FindSystemByName jumps to its entry when nothing is typed
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

// A description's control codes, in the order textControlCodes (DS:9763) lists their handlers.
constexpr std::uint8_t INSERT_SYSTEM_NAME = 1;
constexpr std::uint8_t INSERT_SYSTEM_ADJECTIVE = 2;
constexpr std::uint8_t INSERT_RANDOM_NAME = 3;
constexpr std::uint8_t BACKSPACE_DESCRIPTION = 4;
constexpr std::uint8_t START_CAPITALIZING = 5;
constexpr std::uint8_t STOP_CAPITALIZING = 6;

constexpr std::uint8_t SPACE = 0x20;
constexpr std::uint8_t TEXT_LAYOUT = 2; // screenLayout of the docked text screens
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
// then a label's text, as the index in pendingChartLabels of its first letter, or a disc's centre, and a disc's radius or a
// label's mark in the last byte, which says which of the two the item is. The original holds the text's address there.
constexpr std::uint8_t CHART_ITEM_BYTES = 8;
constexpr std::uint16_t CHART_ITEM_X = 0;
constexpr std::uint16_t CHART_ITEM_ROWS = 2;
constexpr std::uint16_t CHART_ITEM_TEXT = 4;
constexpr std::uint16_t CHART_ITEM_RADIUS = 6; // a disc's, written as a word: the label's mark after it is 0
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
constexpr std::uint8_t DISTANCE_ZEROS_BLANKED = 3;
constexpr std::uint8_t UPPER_CASE_MASK = 0xDF;
constexpr std::uint8_t LOWER_CASE_BIT = 0x20;

// A system's name: up to four pairs of letters from systemNameDigrams, the fourth only when this bit of systemSeed0 is set.
constexpr std::uint16_t SYSTEM_NAME_PAIRS = 4;
constexpr std::uint16_t SYSTEM_NAME_FOURTH_PAIR = 0x40;

// Where the data screen prints (text offsets), and the texts it writes into, in buffers the symbol table names no row for; its
// labels are constants (GalaxyTables.h).
constexpr std::uint16_t DATA_TITLE = 0x0054;
constexpr std::uint16_t DISTANCE_ROW = 0x0144;
constexpr std::uint16_t ECONOMY_ROW = 0x01E4;
constexpr std::uint16_t GOVERNMENT_ROW = 0x0284;
constexpr std::uint16_t TECH_LEVEL_ROW = 0x0324;
constexpr std::uint16_t POPULATION_DIGITS = 0x7D7D; // '000.0 Billion'
constexpr std::size_t POPULATION_SHOWN = 2;         // the units digit, where the point goes after it
constexpr std::uint16_t POPULATION_ROW = 0x03C4;
constexpr std::uint16_t SPECIES_ROW = 0x0414;
constexpr std::uint8_t NO_SPECIES = 0xFF;
constexpr std::uint16_t PRODUCTIVITY_ROW = 0x04B4;
constexpr std::uint16_t RADIUS_TEXT = 0x7EAE; // 'Radius (Av): 00000 km', its digits from data7EBB
constexpr std::uint16_t RADIUS_ROW = 0x0554;
constexpr std::uint8_t TECH_LEVEL_TENS = 10;

// FormatSelectedSystemDistance's text ends here, its tenths digit last.
constexpr std::uint16_t SELECTED_DISTANCE_TEXT_END = 0x7D5F;
// Where InsertRandomName keeps selectedSystemName while it borrows it: DS:9650, in invasionDebriefText's row.
constexpr std::size_t SAVED_SYSTEM_NAME = 0x9650 - 0x94B9;
constexpr std::uint16_t SYSTEM_NAME_BYTES = 8;
constexpr std::uint16_t ADJECTIVE_SUFFIX_BYTES = 5;
// The five digits FormatDecimal5 writes.
constexpr std::size_t DECIMAL_DIGITS = 5;
// A label in pendingChartLabels: its x range and its rows, two bytes each, then its name.
constexpr std::size_t CHART_LABEL_NAME = 4;

constexpr std::uint16_t DESCRIPTION_BUFFER_WORDS = 0x80;
constexpr std::uint16_t DESCRIPTION_SCREEN_OFFSET = 0x5F4;
constexpr std::uint16_t DESCRIPTION_LINE_CHARACTERS = 0x24;
constexpr std::uint16_t TEXT_ROW_BYTES = 0x50;
constexpr std::uint8_t DESCRIPTION_PHRASE_CODE = 0x80;
constexpr std::uint8_t SEVEN_NAMES = 7; // the bits DescribeSystem gives the economy, the government and each of the species' bytes
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
[[nodiscard]] constexpr std::uint16_t Word(ChartSpan _span) noexcept
{
  return Join(_span.last, _span.first);
}

// The two bytes of a span at _bytes[_at], the first below the last.
[[nodiscard]] ChartSpan SpanIn(std::span<const Counted<std::uint8_t>> _bytes, std::size_t _at) noexcept
{
  return ChartSpan{_bytes[_at], _bytes[_at + 1]};
}

// The loop of byte moves (mov al,[si]; mov [di],al; inc si; inc di; loop) some routines make: each of _from's bytes to the
// same place of _to, in order.
void CopyBytes(std::span<const Counted<std::uint8_t>> _from, std::span<Counted<std::uint8_t>> _to)
{
  for (std::size_t done = 0; done < _from.size(); ++done)
  {
    _to[done] = std::uint8_t{_from[done]};
  }
}

// Item _index of chartItems, the item chartItemEnd numbers, as its eight bytes. Nothing in the original bounds the items at the
// 32 the symbol table sizes: a 33rd is the bytes after them, chartLabelCursor's first, as the original would write it.
template <typename State> [[nodiscard]] auto ChartItem(State& _state, std::size_t _index)
{
  return _state.BytesFrom(_state.Data().chartItems, (_index + 1) * CHART_ITEM_BYTES).subspan(_index * CHART_ITEM_BYTES);
}

// _count bytes of pendingChartLabels from its byte _index, where chartLabelCursor and a label's item number it. Nothing in the
// original bounds the labels at the 256 bytes the symbol table sizes either.
template <typename State> [[nodiscard]] auto LabelBytes(State& _state, std::size_t _index, std::size_t _count)
{
  return _state.BytesFrom(_state.Data().pendingChartLabels, _index + _count).subspan(_index);
}

// One pass of PlaceChartLabels over chartItems (CS:152B): whether an item overlaps the label with x range _x and
// rows _rows. It stops at the first that does, and LOOP counts chartItemCount items, 65,536 for 0.
[[nodiscard]] bool AnyChartItemOverlaps(const GameState& _state, ChartSpan _x, ChartSpan _rows)
{
  std::size_t item = 0;
  std::uint16_t itemsLeft = _state.Data().chartItemCount;
  do
  {
    if (ChartItemOverlaps(_state, item, _x, _rows))
    {
      return true;
    }
    ++item;
  } while (--itemsLeft != 0);
  return false;
}

// jmp [bx] through textControlCodes to control code _code's handler, with SI and the return to ResumeTextExpansion pushed; the
// handler returns there, which pops SI. The stack keeps no more than SI and the way back, so the handler's value routine runs on
// the output at DS:_output. Codes 7-31 jump into descriptionPhraseLists, whose words the original would run as code; the
// routine runs nothing for them. No text in the game holds such a code (Tools/GenerateTables.py checks the phrases), and no
// description reaches textControlCodes to write over it: the longest is 144 bytes of descriptionBuffer's 256. Returns the place
// past what the handler wrote.
std::uint16_t RunTextControlCode(GameState& _state, std::uint8_t _code, std::uint16_t _output)
{
  switch (_code)
  {
  case INSERT_SYSTEM_NAME:
    return InsertSystemName(_state, _output);
  case INSERT_SYSTEM_ADJECTIVE:
    return InsertSystemAdjective(_state, _output);
  case INSERT_RANDOM_NAME:
    return InsertRandomName(_state, _output);
  case BACKSPACE_DESCRIPTION:
    return BackspaceDescription(_output);
  case START_CAPITALIZING:
    StartCapitalizing(_state);
    return _output;
  case STOP_CAPITALIZING:
    StopCapitalizing(_state);
    return _output;
  default:
    return _output;
  }
}

// A chart's cross (CS:0D33, CS:0D61, CS:0FAD): _arm either way of (_x, _row), across and then down, each a clipped line. The
// original keeps the centre on the stack between them (PUSH BX / PUSH DX). Returns whether either line's DrawLine filled a
// horizontal line's bytes, after which the direction flag is clear.
bool DrawCross(GameState& _state, std::uint16_t _x, std::uint16_t _row, std::uint16_t _arm)
{
  const bool across = DrawClippedLine(_state, Offset(_x, _arm), _row, static_cast<std::uint16_t>(_x - _arm), _row);
  const bool down = DrawClippedLine(_state, _x, Offset(_row, _arm), _x, static_cast<std::uint16_t>(_row - _arm));
  return across || down;
}

// The chart cursor's cross in colour 1 and the dot at its centre in colour 0 (CS:0D4D, CS:0F99). Returns whether a line of the
// cross filled, as DrawCross.
bool DrawChartCursor(GameState& _state)
{
  _state.Data().drawColor = 1;
  const bool filled = DrawCross(_state, _state.Data().commanderBlock.chartCursorX, _state.Data().commanderBlock.chartCursorY, CURSOR_ARM);
  _state.Data().drawColor = 0;
  PlotPixel(_state, _state.Data().commanderBlock.chartCursorX, _state.Data().commanderBlock.chartCursorY);
  return filled;
}

// The steering moves a chart's cursor (CS:0DB4, CS:0FDA): the roll across, clamped to the chart, and the pitch negated down,
// clamped to its 128 rows. Returns the cursor, which the original leaves in AX.
ChartPoint MoveChartCursor(GameState& _state, Hardware& _hardware)
{
  const Steering steering = ReadSteering(_state, _hardware);
  const std::uint8_t across = steering.roll;
  const unsigned x = unsigned{across} + unsigned{_state.Data().commanderBlock.chartCursorX};
  ChartPoint cursor{};
  if ((across & 0x80) == 0)
  {
    cursor.x = x > 0xFF ? std::uint8_t{0xFF} : static_cast<std::uint8_t>(x);
  }
  else
  {
    // Adding a negative byte carries unless it goes below 0.
    cursor.x = x > 0xFF ? static_cast<std::uint8_t>(x) : std::uint8_t{0};
  }
  _state.Data().commanderBlock.chartCursorX = cursor.x;
  const std::uint8_t down = Negate(steering.pitch);
  const unsigned y = unsigned{down} + unsigned{_state.Data().commanderBlock.chartCursorY};
  if ((down & 0x80) == 0)
  {
    const auto row = static_cast<std::uint8_t>(y);
    cursor.row = row < CHART_ROWS ? row : CURSOR_LOWEST_ROW;
  }
  else
  {
    cursor.row = y > 0xFF ? static_cast<std::uint8_t>(y) : std::uint8_t{0};
  }
  _state.Data().commanderBlock.chartCursorY = cursor.row;
  return cursor;
}

// One frame of the galactic chart up to its presentation (CS:0CF9): the fuel range about the current system, its cross, the
// cursor, and a dot for each of the galaxy's 256 systems, the seeds twisted four times from one to the next. _backward is the
// direction flag, which DrawDisc goes by. The dots' loop carries its count, in AL, and reports its turns to _hardware.
void DrawGalacticChart(GameState& _state, Hardware& _hardware, bool _backward)
{
  LoadGalaxySeeds(_state);
  const auto fuelRadius = static_cast<std::uint16_t>((_state.Data().commanderBlock.fuel >> 3) + 3);
  _state.Data().drawColor = 2;
  DrawDisc(_state, fuelRadius, _state.Data().commanderBlock.currentSystemX, _state.Data().commanderBlock.currentSystemChartY, _backward);
  _state.Data().drawColor = 0;
  (void)DrawCross(_state, _state.Data().commanderBlock.currentSystemX, _state.Data().commanderBlock.currentSystemChartY,
                  CURRENT_SYSTEM_ARM);
  (void)DrawChartCursor(_state);
  _state.Data().drawColor = CHART_DISC_COLOR;
  // XOR AL,AL, then PUSH AX / POP AX round each dot.
  for (std::uint8_t systems = 0;;)
  {
    PlotPixel(_state, _state.Data().systemSeed1Bytes.systemX, static_cast<std::uint8_t>(_state.Data().systemSeed0Bytes.systemY >> 1));
    TwistSystemSeeds(_state);
    TwistSystemSeeds(_state);
    TwistSystemSeeds(_state);
    TwistSystemSeeds(_state);
    systems = static_cast<std::uint8_t>(systems + 1);
    if (systems == 0)
    {
      return;
    }
    _hardware.LoopTurn(GALACTIC_SYSTEM_DOT, {systems});
  }
}

// One frame of the short-range chart up to its presentation (CS:0F60): the fuel range about the centre, the current system's
// cross, the chart's discs and labels, and the cursor. _backward is the direction flag, which DrawDisc goes by; the cross's
// horizontal line clears it before the chart's discs.
void DrawShortRangeChart(GameState& _state, Hardware& /*_hardware*/, bool _backward)
{
  const auto fuelRadius = static_cast<std::uint16_t>(_state.Data().commanderBlock.fuel >> 1);
  _state.Data().drawColor = 2;
  DrawDisc(_state, fuelRadius, SHORT_RANGE_CENTER_X, SHORT_RANGE_CENTER_ROW, _backward);
  _state.Data().drawColor = 0;
  // The current system's cross, from (50h, 51h) to (50h, 2Fh) and from (61h, 40h) to (3Fh, 40h).
  const bool downFilled = DrawLine(_state, 0x50, 0x51, 0x50, 0x2F);
  const bool acrossFilled = DrawLine(_state, 0x61, 0x40, 0x3F, 0x40);
  _state.Data().drawColor = CHART_DISC_COLOR;
  (void)DrawChartItems(_state, _backward && !downFilled && !acrossFilled);
  (void)DrawChartCursor(_state);
}

// What tells the two charts apart: how a frame is drawn and paced, where the loop jumps back to, and the keys.
struct ChartKeys
{
  void (*drawFrame)(GameState&, Hardware&, bool);
  PacingPoint pacing;    // the IBM PC's redraw (D18), paid before PresentChartFrame
  std::uint16_t frame;   // the start of the chart's frame, where its loop jumps back to
  std::uint16_t ignored; // the jump to it that the key tests reach (CS:0E0A, CS:1030)
  std::uint8_t ownKey;   // the chart's own F key, which leaves it open
  void (*recenter)(GameState&);
  bool recenterThroughAl;                                       // the recentring goes through AL, and leaves the cursor's row there
  Counted<std::uint8_t> GameData::CommanderBlock::*keptCursorX; // where the cursor is kept while the chart is closed
  Counted<std::uint8_t> GameData::CommanderBlock::*keptCursorY;
};

// The galactic chart's cursor back on the current system (CS:0E19).
void RecenterGalacticCursor(GameState& _state)
{
  _state.Data().commanderBlock.chartCursorX = std::uint8_t{_state.Data().commanderBlock.currentSystemX};
  _state.Data().commanderBlock.chartCursorY = std::uint8_t{_state.Data().commanderBlock.currentSystemChartY};
}

// The short-range chart's cursor back on its centre (CS:103F).
void RecenterShortRangeCursor(GameState& _state)
{
  _state.Data().commanderBlock.chartCursorX = static_cast<std::uint8_t>(SHORT_RANGE_CENTER_X);
  _state.Data().commanderBlock.chartCursorY = static_cast<std::uint8_t>(SHORT_RANGE_CENTER_ROW);
}

constexpr ChartKeys GALACTIC_CHART_KEYS{.drawFrame = &DrawGalacticChart,
                                        .pacing = GALACTIC_CHART_PACING,
                                        .frame = GALACTIC_CHART_FRAME,
                                        .ignored = GALACTIC_KEY_IGNORED,
                                        .ownKey = SCAN_F5,
                                        .recenter = &RecenterGalacticCursor,
                                        .recenterThroughAl = true,
                                        .keptCursorX = &GameData::CommanderBlock::galacticCursorX,
                                        .keptCursorY = &GameData::CommanderBlock::galacticCursorY};
constexpr ChartKeys SHORT_RANGE_CHART_KEYS{.drawFrame = &DrawShortRangeChart,
                                           .pacing = SHORT_RANGE_CHART_PACING,
                                           .frame = SHORT_RANGE_CHART_FRAME,
                                           .ignored = SHORT_RANGE_KEY_IGNORED,
                                           .ownKey = SCAN_F6,
                                           .recenter = &RecenterShortRangeCursor,
                                           .recenterThroughAl = false,
                                           .keptCursorX = &GameData::CommanderBlock::shortRangeCursorX,
                                           .keptCursorY = &GameData::CommanderBlock::shortRangeCursorY};

// A chart's key (CS:0DEF, CS:1015), with AL _al, the cursor's x as the frame leaves it: D shows the distance to the system
// nearest the cursor, F finds one by name, keypad 5 or fire recentres the cursor, and Esc or an F key other than the chart's
// own closes it, selecting the system at the cursor unless the hyperspace countdown runs. Returns the key that closes it, with
// AL as GetKey and the recentring leave it and BP as the original leaves it. Otherwise it takes the turns the original takes back
// to the chart's frame, which carry nothing: the frame loads every register it reads, and the direction flag is clear from the
// first PresentChartFrame on.
std::optional<ScreenKey> ReadChartKey(GameState& _state, Hardware& _hardware, const ChartKeys& _chart, std::uint8_t _al)
{
  const KeyPress key = GetKey(_state, _hardware);
  std::uint8_t al = AlAfterKey(_al, key);
  const std::uint8_t scanCode = key.scanCode;
  if (scanCode == 0)
  {
    _hardware.LoopTurn(_chart.frame, {});
    return std::nullopt;
  }
  // PresentChartFrame leaves ES on the screen, BP the words of a line it copied and the direction flag clear; the steering and
  // GetKey keep them.
  if (scanCode == SCAN_D)
  {
    ShowNearestSystemDistance(_state, PRESENT_CHART_FRAME_BP, GameState::VIDEO_SEGMENT);
    _hardware.LoopTurn(_chart.frame, {});
    return std::nullopt;
  }
  if (scanCode == SCAN_F)
  {
    FindSystemByName(_state, _hardware, GameState::VIDEO_SEGMENT, PRESENT_CHART_FRAME_BP, false);
    _hardware.LoopTurn(_chart.frame, {});
    return std::nullopt;
  }
  // PUSH AX / POP AX round ReadFireButton keep the key.
  if (scanCode == SCAN_KEYPAD_5 || ReadFireButton(_state, _hardware))
  {
    _chart.recenter(_state);
    if (_chart.recenterThroughAl)
    {
      al = _state.Data().commanderBlock.chartCursorY;
    }
  }
  const bool closes = scanCode == SCAN_ESCAPE || (scanCode >= SCAN_F1 && scanCode <= SCAN_F10 && scanCode != _chart.ownKey);
  if (!closes)
  {
    _hardware.LoopTurn(_chart.ignored, {});
    _hardware.LoopTurn(_chart.frame, {});
    return std::nullopt;
  }
  // PUSH AX / POP AX round the selection keep the key.
  std::uint16_t countLeft = PRESENT_CHART_FRAME_BP;
  if (_state.Data().hyperspaceCountdown == 0)
  {
    countLeft = SelectSystemAtCursor(_state, PRESENT_CHART_FRAME_BP);
    GameData::CommanderBlock& commander = _state.Data().commanderBlock;
    commander.*_chart.keptCursorX = std::uint8_t{commander.chartCursorX};
    commander.*_chart.keptCursorY = std::uint8_t{commander.chartCursorY};
  }
  return ScreenKey{scanCode, al, countLeft};
}

// A chart's frames until a key closes it (CS:0CF9, CS:0F60): the frame drawn, the IBM PC's redraw paid (D18), presented, the
// cursor steered with AL 0, as PresentChartFrame's MOV AX,B800h leaves it, and the key read. _backward is the direction flag on
// the first frame; PresentChartFrame clears it for the rest.
ScreenKey RunChartFrames(GameState& _state, Hardware& _hardware, const ChartKeys& _chart, bool _backward)
{
  for (bool backward = _backward;; backward = false)
  {
    _chart.drawFrame(_state, _hardware, backward);
    _hardware.Spend(_chart.pacing);
    PresentChartFrame(_state, _hardware);
    const ChartPoint cursor = MoveChartCursor(_state, _hardware);
    if (const std::optional<ScreenKey> key = ReadChartKey(_state, _hardware, _chart, cursor.x))
    {
      return *key;
    }
  }
}

// What both charts draw first: _title in colour 1 on no paper (textPaperPattern 0) at the top of the chart, and the two text
// lines under it cleared, at _segment, the screen as DrawChartFrame leaves ES.
void DrawChartTitle(GameState& _state, std::string_view _title, std::uint16_t _segment)
{
  _state.Data().textPaperPattern = 0;
  (void)DrawScreenString(_state, _title, INK_1, _segment, CHART_TITLE);
  (void)ClearChartTextLines(_state, INK_1, _segment);
}

// The segment a chart draws its title in: B800h once DrawChartFrame has made the screen ready (MOV ES,AX), otherwise _segment,
// the ES the chart was entered with.
[[nodiscard]] std::uint16_t ChartSegment(ScreenChange _change, std::uint16_t _segment) noexcept
{
  return _change == ScreenChange::None ? _segment : GameState::VIDEO_SEGMENT;
}

// ShowShortRangeChart's item for the system in systemSeeds (CS:0E9F), _x and _row its offsets from the current system: a
// disc of radius 4 or 6 at 3.5 times them about the centre, and a label to place to its right, the name GenerateSystemName
// makes. The seeds are left on the next system. The disc's centre, which the original keeps on the stack for the label
// (PUSH CX / PUSH DX, then POP DX / POP BX), is a local; its loops' turns go to _hardware (ADR-015).
void AddShortRangeSystem(GameState& _state, Hardware& _hardware, std::int16_t _x, std::int16_t _row)
{
  const std::uint8_t centerX = Low(GalaxyToChart(Word(_x), SHORT_RANGE_CENTER_X));
  const std::uint8_t centerRow = Low(GalaxyToChart(Word(_row), SHORT_RANGE_CENTER_ROW));
  const std::uint16_t itemIndex = _state.Data().chartItemEnd;
  const std::span<Counted<std::uint8_t>> item = ChartItem(_state, itemIndex);
  SetWordIn(item, CHART_ITEM_TEXT, Join(centerRow, centerX));
  const auto radius = static_cast<std::uint16_t>(((_state.Data().systemSeed0Bytes.systemY & 1) << 1) + 4);
  SetWordIn(item, CHART_ITEM_RADIUS, radius);
  // Its box: half the radius either side, as add al,dl / xchg / sub al,dl / neg al make it.
  const auto half = static_cast<std::uint8_t>(radius >> 1);
  std::size_t at = CHART_ITEM_X;
  for (const std::uint8_t center : {centerX, centerRow})
  {
    SetWordIn(item, at, Join(static_cast<std::uint8_t>(half + center), static_cast<std::uint8_t>(center - half)));
    at += 2;
  }
  _state.Data().chartItemEnd = static_cast<std::uint16_t>(itemIndex + 1);
  _state.Data().chartItemCount = static_cast<std::uint8_t>(_state.Data().chartItemCount + 1);
  const std::uint8_t length = GenerateSystemName(_state);

  // The label: from seven pixels right of the disc's centre, six a letter, and from two rows above it to three below.
  const auto left = static_cast<std::uint8_t>(centerX + LABEL_OFFSET_X);
  const ChartSpan x{left, static_cast<std::uint8_t>(left + Low(static_cast<std::uint16_t>(SMALL_LETTER_PIXELS * length)))};
  // The label's bytes, its name LOOP counts in CX, 65,536 letters for 0, and its NUL.
  const std::uint16_t cursor = _state.Data().chartLabelCursor;
  const std::uint32_t letters = LoopCount(length);
  const std::span<const Counted<std::uint8_t>> name = _state.BytesFrom(_state.Data().commanderBlock.selectedSystemName, letters);
  const std::span<Counted<std::uint8_t>> label = LabelBytes(_state, cursor, CHART_LABEL_NAME + letters + 1);
  SetWordIn(label, CHART_ITEM_X, Word(x));
  ChartSpan rows{static_cast<std::uint8_t>(centerRow - LABEL_ABOVE), centerRow};
  if ((rows.first & 0x80) != 0)
  {
    // A label above the chart's top would loop here for ever: the jump goes back past the test. No system on the chart is
    // near enough the top (its rows are 4-7Bh).
    for (;;)
    {
      rows.last = static_cast<std::uint8_t>(rows.last + 1);
      rows.first = static_cast<std::uint8_t>(rows.first + 1);
      _hardware.LoopTurn(SHORT_RANGE_LABEL_BELOW_TOP, {Word(rows)});
    }
  }
  rows.last = static_cast<std::uint8_t>(rows.last + LABEL_BELOW);
  while (rows.last >= CHART_ROWS)
  {
    rows.first = static_cast<std::uint8_t>(rows.first - 1);
    rows.last = static_cast<std::uint8_t>(rows.last - 1);
    _hardware.LoopTurn(SHORT_RANGE_LABEL_ABOVE_BOTTOM, {Word(rows)});
  }
  SetWordIn(label, CHART_ITEM_ROWS, Word(rows));
  // The name, a byte at a time, LOOP counting its length in CX; then the NUL, from CH. Each turn carries the two places, SI and
  // DI, and the count.
  for (std::uint32_t copied = 0;;)
  {
    label[CHART_LABEL_NAME + copied] = std::uint8_t{name[copied]};
    ++copied;
    if (copied == letters)
    {
      break;
    }
    _hardware.LoopTurn(SHORT_RANGE_LABEL_NAME, {_state.OffsetOf(name[copied]), _state.OffsetOf(label[CHART_LABEL_NAME + copied]),
                                                static_cast<std::uint16_t>(letters - copied)});
  }
  label[CHART_LABEL_NAME + letters] = 0;
  _state.Data().chartLabelCursor = static_cast<std::uint16_t>(cursor + CHART_LABEL_NAME + letters + 1);
}

// What REPE CMPSB finds.
struct ByteComparison
{
  std::uint16_t compared; // the bytes it compared, which SI and DI move on by and CX goes down by
  bool equal;             // every byte compared was equal: ZF
};

// REPE CMPSB: DS:_source with _segment:_destination, for at most _count bytes, backwards when _backwards, until
// two differ. A count of 0 compares nothing, and the flags keep what they held.
[[nodiscard]] ByteComparison CompareBytes(const GameState& _state, std::uint16_t _source, std::uint16_t _segment,
                                          std::uint16_t _destination, std::uint16_t _count, bool _backwards)
{
  const auto step = static_cast<std::uint16_t>(_backwards ? 0xFFFF : 1);
  ByteComparison comparison{0, true};
  std::uint16_t source = _source;
  std::uint16_t destination = _destination;
  while (comparison.compared < _count && comparison.equal)
  {
    const std::uint8_t sourceByte = _state.Byte(source);
    const std::uint8_t destinationByte = _state.FarByte(_segment, destination);
    comparison.equal = sourceByte == destinationByte;
    source = Offset(source, step);
    destination = Offset(destination, step);
    ++comparison.compared;
  }
  return comparison;
}

// FindSystemByName's 'ERROR: <name> not on map!' on the second line under the chart (CS:148B), at _segment: the name as typed
// between the other two parts.
void ShowNotOnMap(GameState& _state, std::uint16_t _segment)
{
  const std::uint16_t prefixEnd =
    DrawScreenString(_state, _state.TextAt(_state.Data().findErrorPrefix), INK_3, _segment, CHART_TEXT_LINE_2);
  const std::uint16_t nameEnd = DrawScreenString(_state, _state.TextAt(_state.Data().findInputEcho), INK_2, _segment, prefixEnd);
  (void)DrawScreenString(_state, _state.TextAt(_state.Data().findErrorSuffix), INK_3, _segment, nameEnd);
}

// FindNearestSystem's search (CS:1292): what it leaves in BP, the count its loop had left at the nearest system on the current
// chart, CL with CH cleared (XOR CH,CH / MOV BP,CX); or _countIfNone, BP as it came, when no system is on the chart. The index it
// selects is 100h less that count.
std::uint16_t SearchNearestSystem(GameState& _state, std::uint16_t _countIfNone)
{
  LoadGalaxySeeds(_state);
  const ChartPoint cursor = GetCursorGalaxyPosition(_state);
  // SI the nearest distance so far, and BP the loop's count, CL, at the nearest.
  std::uint16_t nearest = 0xFFFF;
  std::uint16_t nearestCount = _countIfNone;
  for (std::uint16_t count = GALAXY_SYSTEMS; count != 0; --count)
  {
    // dx^2 + (dy/2)^2 to the cursor, each square a byte multiply; a sum that carries is passed over.
    const std::uint16_t across = Square(Low(Magnitude(static_cast<std::uint16_t>(_state.Data().systemSeed1Bytes.systemX - cursor.x))));
    const std::uint16_t down =
      Square(Low(Magnitude(static_cast<std::uint16_t>((_state.Data().systemSeed0Bytes.systemY >> 1) - cursor.row))));
    const std::uint32_t distance = std::uint32_t{across} + down;
    if (distance <= 0xFFFF && distance < nearest && IsSystemOnChart(_state))
    {
      nearest = static_cast<std::uint16_t>(distance);
      nearestCount = Low(count);
    }
    AdvanceToNextSystem(_state);
  }
  const auto index = static_cast<std::uint8_t>(GALAXY_SYSTEMS - nearestCount);
  _state.Data().commanderBlock.selectedSystemIndex = index;
  LoadSystemSeeds(_state, index);
  MoveCursorToSystem(_state);
  return nearestCount;
}

// What SelectSystemAtCursor and ShowNearestSystemDistance begin with: FindNearestSystem, ComputeDistanceToSystem, and the
// distance, read back from selectedDistanceTenthsLy, into distanceDigits with up to three leading zeros blanked. Returns the BP
// FindNearestSystem leaves.
std::uint16_t SelectNearestSystem(GameState& _state, std::uint16_t _countIfNone)
{
  const std::uint16_t countLeft = SearchNearestSystem(_state, _countIfNone);
  ComputeDistanceToSystem(_state);
  const std::span<Counted<std::uint8_t>> digits = _state.BytesFrom(_state.Data().distanceDigits, DECIMAL_DIGITS);
  FormatDecimal5(_state, _state.Data().commanderBlock.selectedDistanceTenthsLy, digits);
  BlankLeadingZeros(digits, DISTANCE_ZEROS_BLANKED);
  return countLeft;
}

} // namespace

ScreenKey ShowGalacticChart(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _segment)
{
  _state.Data().sunFringeMask = 0;
  LoadSystemSeeds(_state, _state.Data().commanderBlock.currentSystemIndex);
  _state.Data().commanderBlock.currentSystemX = std::uint8_t{_state.Data().systemSeed1Bytes.systemX};
  _state.Data().commanderBlock.currentSystemChartY = static_cast<std::uint8_t>(_state.Data().systemSeed0Bytes.systemY >> 1);
  const std::uint16_t segment = ChartSegment(DrawChartFrame(_state, _hardware, _backward), _segment);
  _state.Data().galacticChartNumber = static_cast<std::uint8_t>(_state.Data().commanderBlock.galaxyNumber + '1');
  DrawChartTitle(_state, _state.TextAt(_state.Data().galacticChartTitle), segment);
  _state.Data().commanderBlock.chartCursorX = std::uint8_t{_state.Data().commanderBlock.galacticCursorX};
  _state.Data().commanderBlock.chartCursorY = std::uint8_t{_state.Data().commanderBlock.galacticCursorY};
  _state.Data().commanderBlock.chartIsShortRange = 0;
  return RunChartFrames(_state, _hardware, GALACTIC_CHART_KEYS, _backward);
}

ScreenKey ShowShortRangeChart(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _segment)
{
  _state.Data().sunFringeMask = 0;
  const std::uint16_t segment = ChartSegment(DrawChartFrame(_state, _hardware, _backward), _segment);
  DrawChartTitle(_state, _state.TextAt(_state.Data().shortRangeChartTitle), segment);
  _state.Data().commanderBlock.chartCursorX = std::uint8_t{_state.Data().commanderBlock.shortRangeCursorX};
  _state.Data().commanderBlock.chartCursorY = std::uint8_t{_state.Data().commanderBlock.shortRangeCursorY};
  _state.Data().commanderBlock.chartIsShortRange = 1;
  _state.Data().chartItemCount = 0;
  _state.Data().chartItemEnd = 0;
  _state.Data().chartLabelCursor = 0;
  LoadGalaxySeeds(_state);
  // Each of the 256 systems that is on the chart becomes an item. XOR AL,AL, then PUSH AX round the system and POP AX at its end,
  // where a system off the chart jumps back to (CS:0F50) with the count on the stack; the jump back to the next system carries it
  // in AL.
  for (std::uint8_t systems = 0;;)
  {
    const ShortRangeOffset offset = GetShortRangeOffset(_state);
    if (offset.onChart)
    {
      // The short-range chart measures both offsets of a system on it.
      AddShortRangeSystem(_state, _hardware, offset.x.value_or(0), offset.row.value_or(0));
    }
    else
    {
      AdvanceToNextSystem(_state);
      _hardware.LoopTurn(SHORT_RANGE_SYSTEM_DONE, {});
    }
    systems = static_cast<std::uint8_t>(systems + 1);
    if (systems == 0)
    {
      break;
    }
    _hardware.LoopTurn(SHORT_RANGE_NEXT_SYSTEM, {systems});
  }
  PlaceChartLabels(_state);
  return RunChartFrames(_state, _hardware, SHORT_RANGE_CHART_KEYS, _backward);
}

SystemSeeds ReadSystemSeeds(const GameState& _state)
{
  return SystemSeeds{_state.Data().systemSeed0, _state.Data().systemSeed1, _state.Data().systemSeed2};
}

ShortRangeOffset GetShortRangeOffset(const GameState& _state)
{
  if (_state.Data().commanderBlock.chartIsShortRange == 0)
  {
    return ShortRangeOffset{.onChart = true, .x = std::nullopt, .row = std::nullopt};
  }
  const SystemSeeds seeds = ReadSystemSeeds(_state);
  const auto x = static_cast<std::int16_t>(seeds.X() - _state.Data().commanderBlock.currentSystemX);
  if (Magnitude(Word(x)) >= SHORT_RANGE_HALF_WIDTH)
  {
    return ShortRangeOffset{.onChart = false, .x = x, .row = std::nullopt};
  }
  const auto row = static_cast<std::int16_t>(seeds.Row() - _state.Data().commanderBlock.currentSystemChartY);
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
  _state.Data().systemSeed1 = seeds.seed2;
  _state.Data().systemSeed0 = seeds.seed1;
  _state.Data().systemSeed2 = static_cast<std::uint16_t>(seeds.seed0 + seeds.seed1 + seeds.seed2);
}

void LoadGalaxySeeds(GameState& _state)
{
  // The table offset is figured in BL, so it wraps at a byte.
  const auto entry = static_cast<std::uint8_t>(_state.Data().commanderBlock.galaxyNumber * GALAXY_SEED_BYTES);
  const std::span<const Counted<std::uint8_t>> seeds =
    _state.BytesFrom(_state.Data().galaxySeeds, std::size_t{entry} + GALAXY_SEED_BYTES).subspan(entry);
  _state.Data().systemSeed0 = WordIn(seeds, 0);
  _state.Data().systemSeed1 = WordIn(seeds, 2);
  _state.Data().systemSeed2 = WordIn(seeds, 4);
}

ChartPoint GetCursorGalaxyPosition(const GameState& _state)
{
  const ChartPoint cursor{_state.Data().commanderBlock.chartCursorX, _state.Data().commanderBlock.chartCursorY};
  if (_state.Data().commanderBlock.chartIsShortRange != 1)
  {
    return cursor;
  }
  const std::uint16_t x =
    ChartToGalaxy(static_cast<std::uint16_t>(cursor.x - SHORT_RANGE_CENTER_X), _state.Data().commanderBlock.currentSystemX);
  const std::uint16_t row =
    ChartToGalaxy(static_cast<std::uint16_t>(cursor.row - SHORT_RANGE_CENTER_ROW), _state.Data().commanderBlock.currentSystemChartY);
  return ChartPoint{Low(x), Low(row)};
}

void MoveCursorToSystem(GameState& _state)
{
  const SystemSeeds seeds = ReadSystemSeeds(_state);
  if (_state.Data().commanderBlock.chartIsShortRange != 1)
  {
    _state.Data().commanderBlock.chartCursorX = seeds.X();
    _state.Data().commanderBlock.chartCursorY = seeds.Row();
    return;
  }
  const auto across = static_cast<std::uint16_t>(seeds.X() - _state.Data().commanderBlock.currentSystemX);
  _state.Data().commanderBlock.chartCursorX = Low(GalaxyToChart(across, SHORT_RANGE_CENTER_X));
  const auto down = static_cast<std::uint16_t>(seeds.Row() - _state.Data().commanderBlock.currentSystemChartY);
  _state.Data().commanderBlock.chartCursorY = Low(GalaxyToChart(down, SHORT_RANGE_CENTER_ROW));
}

std::uint16_t SelectSystemAtCursor(GameState& _state, std::uint16_t _countIfNone)
{
  const std::uint16_t countLeft = SelectNearestSystem(_state, _countIfNone);
  DescribeSystem(_state);
  return countLeft;
}

void DescribeSystem(GameState& _state)
{
  const std::uint8_t seed1 = Low(_state.Data().systemSeed1);
  const std::uint8_t seed2 = Low(_state.Data().systemSeed2);
  const std::uint8_t seed2High = _state.Data().systemSeed2Bytes.systemSeed2High;
  const std::uint8_t x = _state.Data().systemSeed1Bytes.systemX;
  const std::uint8_t y = _state.Data().systemSeed0Bytes.systemY;

  const auto government = static_cast<std::uint8_t>((seed1 >> 3) & 7);
  _state.Data().commanderBlock.selectedGovernment = government;
  // Governments 0 and 1 set bit 1 of the economy before it is inverted.
  const auto economy = static_cast<std::uint8_t>((((government >> 1) == 0 ? 2 : 0) | (y & 7)) ^ 7);
  _state.Data().commanderBlock.selectedEconomy = economy;
  const auto techLevel = static_cast<std::uint8_t>(((economy + 3) & x) + (seed2 & 1));
  _state.Data().commanderBlock.selectedTechLevel = techLevel;
  const auto population =
    static_cast<std::uint8_t>((Low(static_cast<std::uint16_t>(techLevel * economy)) >> 1) + 0x14 + ((seed1 >> 3) & 7));
  _state.Data().commanderBlock.selectedPopulationTenthsBillion = population;

  if ((seed2 & 0x80) == 0)
  {
    _state.Data().commanderBlock.selectedSpeciesAdjective1 = 0xFF;
  }
  else
  {
    const auto mixed = static_cast<std::uint8_t>((x ^ y) & 7);
    _state.Data().commanderBlock.selectedSpeciesAdjective1 = static_cast<std::uint8_t>((seed2High >> 2) & 7);
    _state.Data().commanderBlock.selectedSpeciesAdjective2 = static_cast<std::uint8_t>(seed2High >> 5);
    _state.Data().commanderBlock.selectedSpeciesAdjective3 = mixed;
    _state.Data().commanderBlock.selectedSpeciesType = static_cast<std::uint8_t>(((seed2High & 3) + mixed) & 7);
  }

  // (government+8)^2 in AL, times the population, times 4.
  const std::uint8_t squared = Low(Square(static_cast<std::uint8_t>(government + 8)));
  _state.Data().commanderBlock.selectedProductivityMillionCredits = static_cast<std::uint16_t>((squared * population) << 2);

  const std::uint16_t rows = MakeWord(y, y);
  const auto rotated = static_cast<std::uint16_t>((rows << 2) | (rows >> 14));
  const std::uint16_t seed2Word = _state.Data().systemSeed2;
  const auto swapped = static_cast<std::uint16_t>(((seed2Word >> 8) | (seed2Word << 8)) & 0x3FF);
  _state.Data().commanderBlock.selectedRadiusKm = static_cast<std::uint16_t>(((rotated ^ swapped) & 0xFFF) + 0x10E1);

  const auto descriptionSeed0 = static_cast<std::uint16_t>(_state.Data().systemSeed0 ^ _state.Data().systemSeed1);
  _state.Data().commanderBlock.descriptionSeed0 = descriptionSeed0;
  _state.Data().commanderBlock.descriptionSeed1 = static_cast<std::uint16_t>(descriptionSeed0 ^ _state.Data().systemSeed2);
  GenerateSystemName(_state);
}

std::uint8_t FindNearestSystem(GameState& _state, std::uint16_t _countIfNone)
{
  return static_cast<std::uint8_t>(GALAXY_SYSTEMS - SearchNearestSystem(_state, _countIfNone));
}

std::uint16_t ComputeDistanceToSystem(GameState& _state)
{
  const SystemSeeds seeds = ReadSystemSeeds(_state);
  // Each difference made positive, and its low byte squared by a byte multiply.
  const std::uint16_t across = Square(Low(Magnitude(static_cast<std::uint16_t>(seeds.X() - _state.Data().commanderBlock.currentSystemX))));
  const std::uint16_t down =
    Square(Low(Magnitude(static_cast<std::uint16_t>(seeds.Row() - _state.Data().commanderBlock.currentSystemChartY))));
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
  _state.Data().commanderBlock.selectedDistanceTenthsLy = distanceTenthsLy;
  return distanceTenthsLy;
}

void ShowNearestSystemDistance(GameState& _state, std::uint16_t _countIfNone, std::uint16_t _segment)
{
  (void)SelectNearestSystem(_state, _countIfNone);
  // The distance's four digits into distanceText, either side of its point.
  constexpr std::array<std::size_t, 4> PLACES = {DISTANCE_TEXT_HUNDREDS, DISTANCE_TEXT_TENS, DISTANCE_TEXT_UNITS, DISTANCE_TEXT_TENTHS};
  constexpr std::array<Counted<std::uint8_t> GameData::DistanceDigits::*, 4> DIGITS = {
    &GameData::DistanceDigits::distanceHundredsDigit, &GameData::DistanceDigits::distanceTensDigit,
    &GameData::DistanceDigits::distanceUnitsDigit, &GameData::DistanceDigits::distanceTenthsDigit};
  for (std::size_t digit = 0; digit < DIGITS.size(); ++digit)
  {
    _state.Data().distanceText[PLACES[digit]] = std::uint8_t{_state.Data().distanceDigits.*DIGITS[digit]};
  }
  _state.Data().textPaperPattern = 0;
  DrawScreenString(_state, _state.TextAt(_state.Data().distanceText), INK_3, _segment, CHART_TEXT_LINE_2);
  GenerateSystemName(_state);
  DrawScreenString(_state, _state.TextAt(_state.Data().commanderBlock.selectedSystemName), INK_2, _segment, CHART_TEXT_LINE_1);
  DrawScreenString(_state, _state.TextAt(_state.Data().nameLinePadding), INK_2, _segment, CHART_NAME_PADDING);
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
  _state.Data().commanderBlock.selectedSystemName.data75CA = 0x2020;
  _state.Data().commanderBlock.selectedSystemName.data75C8 = 0x2020;
  // The fourth pair only when bit 6 of systemSeed0 was set.
  const bool fourPairs = (_state.Data().systemSeed0 & SYSTEM_NAME_FOURTH_PAIR) != 0;
  const std::span<Counted<std::uint8_t>> name = _state.BytesFrom(_state.Data().commanderBlock.selectedSystemName, SYSTEM_NAME_BYTES);
  std::size_t at = 0;
  std::uint8_t length = 0;
  for (std::uint16_t pair = 0; pair < SYSTEM_NAME_PAIRS; ++pair)
  {
    const std::uint8_t pick = _state.Data().systemSeed2Bytes.systemSeed2High;
    TwistSystemSeeds(_state);
    const Counted<std::uint8_t>(&letters)[2] = _state.Data().systemNameDigrams[pick & 0x1F];
    const std::uint16_t digram = MakeWord(letters[0], letters[1]);
    if (pair + 1 == SYSTEM_NAME_PAIRS && !fourPairs)
    {
      continue;
    }
    // A word: a letter of the pair that is a space is written, and the next pair overwrites it.
    SetWordIn(name, at, digram);
    if (High(digram) != SPACE)
    {
      ++at;
      ++length;
    }
    if (Low(digram) != SPACE)
    {
      ++at;
      ++length;
    }
  }
  _state.Data().commanderBlock.selectedSystemNameLength = length;
  return length;
}

void FindSystemByName(GameState& _state, Hardware& _hardware, std::uint16_t _segment, std::uint16_t _countIfNone, bool _backward)
{
  LoadGalaxySeeds(_state);
  DrawScreenString(_state, _state.TextAt(_state.Data().findPrompt), INK_1, _segment, CHART_TEXT_LINE_1);
  DrawScreenString(_state, _state.TextAt(_state.Data().blankChartLine), INK_1, _segment, CHART_TEXT_LINE_2);
  const std::span<Counted<std::uint8_t>> typed(_state.Data().findInput);
  for (std::size_t word = 0; word < SYSTEM_NAME_BYTES; word += 2)
  {
    SetWordIn(typed, word, 0);
  }
  (void)ReadTextLine(_state, _hardware, typed, static_cast<std::uint8_t>(SYSTEM_NAME_BYTES), _segment, CHART_TEXT_LINE_2);
  if (typed[0] == 0)
  {
    // Nothing typed: on into ShowNearestSystemDistance, by a jump back to its entry, with ES as ReadTextLine leaves it.
    _hardware.LoopTurn(SHOW_NEAREST_SYSTEM_DISTANCE, {});
    const bool textLayout = _state.Data().screenLayout == TEXT_LAYOUT;
    ShowNearestSystemDistance(_state, _countIfNone, textLayout ? GameState::VIDEO_SEGMENT : _segment);
    return;
  }

  // The name echoed for the error message and upper-cased in place, a space after it; the count to compare its length,
  // counting the space when there is room for it. ReadTextLine leaves a NUL within findInput's nine bytes. Each turn of the loop
  // carries the two places and the length.
  const std::span<Counted<std::uint8_t>> echo(_state.Data().findInputEcho);
  std::uint16_t length = 0;
  for (;;)
  {
    const std::uint8_t character = typed[length];
    echo[length] = character;
    if (character == 0)
    {
      break;
    }
    typed[length] = static_cast<std::uint8_t>(typed[length] & UPPER_CASE_MASK);
    ++length;
    _hardware.LoopTurn(FIND_UPPER_CASE, {_state.OffsetOf(typed[length]), _state.OffsetOf(echo[length]), length});
  }
  typed[length] = SPACE;
  const auto count = static_cast<std::uint16_t>(length == SYSTEM_NAME_BYTES ? length : length + 1);

  // Each system's name in turn, compared with it by REPE CMPSB in the data segment. Each turn carries the system and the count.
  for (std::uint8_t system = 0;;)
  {
    GenerateSystemName(_state);
    if (CompareBytes(_state, _state.OffsetOf(_state.Data().commanderBlock.selectedSystemName), _state.DataSegment(),
                     _state.OffsetOf(_state.Data().findInput), count, _backward)
          .equal)
    {
      LoadGalaxySeeds(_state);
      LoadSystemSeeds(_state, system);
      if (!GetShortRangeOffset(_state).onChart)
      {
        _hardware.LoopTurn(FIND_NOT_ON_MAP, {});
        ShowNotOnMap(_state, GameState::VIDEO_SEGMENT);
        return;
      }
      MoveCursorToSystem(_state);
      ShowNearestSystemDistance(_state, _countIfNone, GameState::VIDEO_SEGMENT);
      return;
    }
    ++system;
    if (system == 0)
    {
      break;
    }
    _hardware.LoopTurn(FIND_NEXT_SYSTEM, {system, count});
  }
  ShowNotOnMap(_state, GameState::VIDEO_SEGMENT);
}

bool DrawChartItems(GameState& _state, bool _backward)
{
  // MOV CL,[chartItemCount] / AND CX,0FFh: CH stays 0, the high byte of a disc's centre row.
  const std::uint8_t items = _state.Data().chartItemCount;
  if (items == 0)
  {
    return false;
  }
  _state.Data().drawColor = CHART_DISC_COLOR;
  bool discDrawn = false;
  // PUSH CX / PUSH DI round each item keep the count and the item: the LOOP's count is a local.
  std::size_t index = 0;
  for (std::uint8_t itemsLeft = items; itemsLeft != 0; --itemsLeft)
  {
    const std::span<const Counted<std::uint8_t>> item = ChartItem(_state, index);
    const std::uint16_t mark = WordIn(item, CHART_ITEM_RADIUS);
    if (High(mark) != 0)
    {
      // A label, its text at its top-left corner: the x range's first byte in DL and the rows' first in DH.
      (void)DrawSmallViewString(_state, _state.TextAt(_state.Data().pendingChartLabels, WordIn(item, CHART_ITEM_TEXT)), INK_3,
                                MakeWord(item[CHART_ITEM_X], item[CHART_ITEM_ROWS]));
    }
    else
    {
      // A disc: the radius in BX, the centre's x in DX and its row in CX.
      const std::uint16_t center = WordIn(item, CHART_ITEM_TEXT);
      DrawDisc(_state, mark, Low(center), High(center), _backward);
      discDrawn = true;
    }
    ++index;
  }
  return discDrawn;
}

void PlaceChartLabels(GameState& _state)
{
  const std::uint8_t labels = _state.Data().chartItemCount;
  if (labels == 0)
  {
    return;
  }
  _state.Data().labelsLeftToPlace = labels;
  _state.Data().chartLabelCursor = 0;
  do
  {
    // The label's x range and rows, then its name.
    const std::uint16_t cursor = _state.Data().chartLabelCursor;
    const std::span<const Counted<std::uint8_t>> label = LabelBytes(_state, cursor, CHART_LABEL_NAME);
    const ChartSpan x = SpanIn(label, CHART_ITEM_X);
    ChartSpan rows = SpanIn(label, CHART_ITEM_ROWS);
    _state.Data().chartLabelCursor = static_cast<std::uint16_t>(cursor + 4);
    _state.Data().labelNudgeCount = 0;
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
    _state.Data().labelsLeftToPlace = static_cast<std::uint8_t>(_state.Data().labelsLeftToPlace - 1);
  } while (_state.Data().labelsLeftToPlace != 0);
}

void AddChartLabel(GameState& _state, ChartSpan _x, ChartSpan _rows)
{
  const std::uint16_t itemIndex = _state.Data().chartItemEnd;
  const std::span<Counted<std::uint8_t>> item = ChartItem(_state, itemIndex);
  SetWordIn(item, CHART_ITEM_X, Word(_x));
  SetWordIn(item, CHART_ITEM_ROWS, Word(_rows));
  const std::uint16_t name = _state.Data().chartLabelCursor;
  SetWordIn(item, CHART_ITEM_TEXT, name);
  item[CHART_ITEM_IS_LABEL] = 1;
  _state.Data().chartItemEnd = static_cast<std::uint16_t>(itemIndex + 1);
  _state.Data().chartItemCount = static_cast<std::uint8_t>(_state.Data().chartItemCount + 1);
  // Past the name's NUL; the scan starts at its second byte.
  const std::size_t rest = _state.TextAt(_state.Data().pendingChartLabels, static_cast<std::uint16_t>(name + 1)).size();
  _state.Data().chartLabelCursor = static_cast<std::uint16_t>(name + 1 + rest + 1);
}

LabelNudge NudgeChartLabel(GameState& _state, ChartSpan _rows)
{
  ChartSpan rows = _rows;
  for (;;)
  {
    const auto step = static_cast<std::uint8_t>(_state.Data().labelNudgeCount + 1);
    _state.Data().labelNudgeCount = step;
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

bool ChartItemOverlaps(const GameState& _state, std::size_t _item, ChartSpan _x, ChartSpan _rows)
{
  const std::span<const Counted<std::uint8_t>> item = ChartItem(_state, _item);
  const ChartSpan itemX = SpanIn(item, CHART_ITEM_X);
  const ChartSpan itemRows = SpanIn(item, CHART_ITEM_ROWS);
  return itemX.last >= _x.first && _x.last >= itemX.first && itemRows.last >= _rows.first && _rows.last >= itemRows.first;
}

std::uint16_t ClearChartTextLines(GameState& _state, std::uint16_t _ink, std::uint16_t _segment)
{
  DrawScreenString(_state, _state.TextAt(_state.Data().blankChartLine), _ink, _segment, CHART_TEXT_LINE_1);
  return DrawScreenString(_state, _state.TextAt(_state.Data().blankChartLine), _ink, _segment, CHART_TEXT_LINE_2);
}

ScreenKey ShowSystemDataScreen(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _countIfNone)
{
  const std::uint16_t titleEnd =
    PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, _state.Data().systemDataFrame, _backward), DATA_TITLE);
  TerminateSelectedSystemName(_state);
  PrintTextModeString(_state, _state.TextAt(_state.Data().commanderBlock.selectedSystemName), titleEnd);
  std::uint16_t printed = PrintTextModeString(_state, DISTANCE_LABEL_TEXT, DISTANCE_ROW);
  printed = PrintTextModeString(_state, FormatSelectedSystemDistance(_state), printed);
  PrintTextModeString(_state, LIGHT_YEARS_TEXT, printed);
  printed = PrintTextModeString(_state, ECONOMY_LABEL_TEXT, ECONOMY_ROW);
  PrintTextModeString(_state, ECONOMY_NAMES[_state.Data().commanderBlock.selectedEconomy & SEVEN_NAMES], printed);
  printed = PrintTextModeString(_state, GOVERNMENT_LABEL_TEXT, GOVERNMENT_ROW);
  PrintTextModeString(_state, GOVERNMENT_NAMES[_state.Data().commanderBlock.selectedGovernment & SEVEN_NAMES], printed);
  printed = PrintTextModeString(_state, TECH_LEVEL_LABEL_TEXT, TECH_LEVEL_ROW);

  // The tech level, one to sixteen, in the two characters at data7D48, a column left when it is one digit.
  auto level = static_cast<std::uint8_t>(_state.Data().commanderBlock.selectedTechLevel + 1);
  auto cell = static_cast<std::uint16_t>(printed - 2);
  _state.Data().data7D48 = SPACE;
  if (level >= TECH_LEVEL_TENS)
  {
    level = static_cast<std::uint8_t>(level - TECH_LEVEL_TENS);
    _state.Data().data7D48 = '1';
    cell = Offset(cell, 2);
  }
  _state.Data().data7D49 = static_cast<std::uint8_t>(level + '0');
  PrintTextModeString(_state, _state.TextAt(_state.Data().data7D48), cell);

  // The population in tenths of a billion, its last two digits about a point.
  const std::span<Counted<std::uint8_t>> population = _state.BytesAt(POPULATION_DIGITS, DECIMAL_DIGITS);
  FormatDecimal5(_state, _state.Data().commanderBlock.selectedPopulationTenthsBillion, population);
  printed = PrintTextModeString(_state, POPULATION_LABEL_TEXT, POPULATION_ROW);
  population[POPULATION_SHOWN] = std::uint8_t{population[POPULATION_SHOWN + 1]};
  population[POPULATION_SHOWN + 1] = DECIMAL_POINT;
  PrintTextModeString(_state, _state.TextAt(population[POPULATION_SHOWN]), printed);

  printed = PrintTextModeString(_state, SPECIES_OPEN_TEXT, SPECIES_ROW);
  const std::uint8_t size = _state.Data().commanderBlock.selectedSpeciesAdjective1;
  if (size == NO_SPECIES)
  {
    PrintTextModeString(_state, HUMAN_COLONIALS_TEXT, printed);
  }
  else
  {
    // Each of the species' four bytes indexes its table (SHL AL,1 / XOR AH,AH). DescribeSystem gives each three bits.
    const GameData::CommanderBlock& commander = _state.Data().commanderBlock;
    cell = PrintTextModeString(_state, SPECIES_SIZE_NAMES[size & SEVEN_NAMES], printed);
    cell = PrintTextModeString(_state, SPECIES_COLOR_NAMES[commander.selectedSpeciesAdjective2 & SEVEN_NAMES], cell);
    cell = PrintTextModeString(_state, SPECIES_TRAIT_NAMES[commander.selectedSpeciesAdjective3 & SEVEN_NAMES], cell);
    PrintTextModeString(_state, SPECIES_TYPE_NAMES[commander.selectedSpeciesType & SEVEN_NAMES], cell);
  }

  // The productivity, its leading zero blanked a column left; the radius, its leading digit blanked.
  FormatDecimal5(_state, _state.Data().commanderBlock.selectedProductivityMillionCredits,
                 _state.BytesFrom(_state.Data().data7EA3, DECIMAL_DIGITS));
  cell = PrintTextModeString(_state, PRODUCTIVITY_LABEL_TEXT, PRODUCTIVITY_ROW);
  if (_state.Data().data7EA3 == '0')
  {
    _state.Data().data7EA3 = SPACE;
    cell = static_cast<std::uint16_t>(cell - 2);
  }
  PrintTextModeString(_state, _state.TextAt(_state.Data().data7EA3), cell);
  FormatDecimal5(_state, _state.Data().commanderBlock.selectedRadiusKm, _state.BytesFrom(_state.Data().data7EBB, DECIMAL_DIGITS));
  _state.Data().data7EBB = SPACE;
  PrintTextModeString(_state, _state.TextAtOffset(RADIUS_TEXT), RADIUS_ROW);
  ShowSystemDescription(_state, _backward);
  // ShowSystemDescription's last print leaves AL 0.
  return WaitForScreenExitKey(_state, _hardware, SCAN_F7, 0, _countIfNone);
}

std::uint8_t TerminateSelectedSystemName(GameState& _state)
{
  const std::uint8_t length = _state.Data().commanderBlock.selectedSystemNameLength;
  _state.BytesFrom(_state.Data().commanderBlock.selectedSystemName, std::size_t{length} + 1)[length] = 0;
  return length;
}

std::string_view FormatSelectedSystemDistance(GameState& _state)
{
  // SI and DI, backwards: the copy runs until it has copied a space, which SelectNearestSystem leaves before the digits.
  std::uint16_t from = _state.OffsetOf(_state.Data().distanceDigits.distanceTenthsDigit);
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
  return _state.TextAtOffset(Offset(to, 1));
}

void ShowSystemDescription(GameState& _state, bool _backward)
{
  // REP STOSW of zeros over descriptionBuffer, ES = DS, forwards or, _backward, down from its start: DI.
  const std::uint16_t buffer = _state.OffsetOf(_state.Data().descriptionBuffer);
  std::uint16_t word = buffer;
  for (std::uint16_t words = DESCRIPTION_BUFFER_WORDS; words != 0; --words)
  {
    _state.SetWord(word, 0);
    word = Offset(word, WordStep(_backward));
  }
  const std::uint16_t expanded = ExpandDescriptionText(_state, _state.TextAt(_state.Data().descriptionTemplate), buffer);
  _state.SetByte(expanded, 0);

  // Word-wrapped at the last space within 36 characters, a text row at a time: SI the line and DI its end.
  std::uint16_t line = buffer;
  std::uint16_t cell = DESCRIPTION_SCREEN_OFFSET;
  for (;;)
  {
    std::uint16_t end = Offset(line, DESCRIPTION_LINE_CHARACTERS);
    if (_state.Byte(end) == 0)
    {
      (void)PrintTextModeString(_state, _state.TextAtOffset(line), cell);
      return;
    }
    while (_state.Byte(end) != SPACE)
    {
      --end;
    }
    _state.SetByte(end, 0);
    const std::string_view text = _state.TextAtOffset(line);
    (void)PrintTextModeString(_state, text, cell);
    line = Offset(line, static_cast<std::uint16_t>(text.size() + 1));
    cell = Offset(cell, TEXT_ROW_BYTES);
  }
}

std::uint16_t ExpandDescriptionText(GameState& _state, std::string_view _text, std::uint16_t _output)
{
  std::uint16_t next = _output;
  for (const char letter : _text)
  {
    const auto code = static_cast<std::uint8_t>(letter);
    if (code < SPACE)
    {
      next = RunTextControlCode(_state, code, next);
      continue;
    }
    if (code >= DESCRIPTION_PHRASE_CODE)
    {
      // One of the five phrases in the code's list, by the next random number's low byte / 52.
      const std::array<std::uint8_t, 5>& list = DESCRIPTION_PHRASE_LISTS[static_cast<std::uint8_t>(code - DESCRIPTION_PHRASE_CODE)];
      const auto pick = static_cast<std::uint8_t>(Low(NextDescriptionRandom(_state)) / DESCRIPTION_PHRASE_DIVISOR);
      next = ExpandDescriptionText(_state, DESCRIPTION_PHRASES[list[pick]], next);
      continue;
    }
    const std::uint16_t output = next;
    const std::uint8_t previous = _state.Byte(static_cast<std::uint16_t>(output - 1));
    if (code == SPACE && previous == SPACE)
    {
      continue;
    }
    // Capitalizing reaches only 0x60-0x78: y and z stay lower case.
    std::uint8_t character = code;
    if (_state.Data().descriptionCapitalize == 1 && previous == SPACE && code >= 0x60 && code < 0x79)
    {
      character = static_cast<std::uint8_t>(code & UPPER_CASE_MASK);
    }
    _state.SetByte(output, character);
    next = Offset(output, 1);
  }
  return next;
}

std::uint16_t InsertSystemName(GameState& _state, std::uint16_t _output)
{
  // MOV AX,20h / MOV [DI],AX: the space and the NUL after the name again, as a word.
  const std::size_t space = CopySelectedNameLower(_state);
  SetWordIn(_state.BytesFrom(_state.Data().descriptionNameBuffer, space + 2), space, SPACE);
  return ExpandDescriptionText(_state, _state.TextAt(_state.Data().descriptionNameBuffer), _output);
}

std::uint16_t InsertSystemAdjective(GameState& _state, std::uint16_t _output)
{
  // The name has a first letter: CopySelectedNameLower copies it whatever it is.
  std::size_t suffix = CopySelectedNameLower(_state);
  const std::span<Counted<std::uint8_t>> name = _state.BytesFrom(_state.Data().descriptionNameBuffer, suffix + ADJECTIVE_SUFFIX_BYTES);
  const std::uint8_t last = name[suffix - 1];
  if (last == 'a' || last == 'e' || last == 'i' || last == 'o' || last == 'u')
  {
    --suffix;
  }
  CopyBytes(_state.Data().adjectiveSuffix, name.subspan(suffix, ADJECTIVE_SUFFIX_BYTES));
  return ExpandDescriptionText(_state, _state.TextAt(_state.Data().descriptionNameBuffer), _output);
}

std::uint16_t InsertRandomName(GameState& _state, std::uint16_t _output)
{
  const std::span<Counted<std::uint8_t>> selected = _state.BytesFrom(_state.Data().commanderBlock.selectedSystemName, SYSTEM_NAME_BYTES);
  const std::span<Counted<std::uint8_t>> saved = std::span(_state.Data().invasionDebriefText).subspan(SAVED_SYSTEM_NAME, SYSTEM_NAME_BYTES);
  CopyBytes(selected, saved);
  const std::uint16_t seed0 = _state.Data().commanderBlock.descriptionSeed0;
  _state.Data().systemSeed0 = seed0;
  const std::uint16_t seed1 = _state.Data().commanderBlock.descriptionSeed1;
  _state.Data().systemSeed1 = seed1;
  _state.Data().systemSeed2 = static_cast<std::uint16_t>(seed1 ^ _state.Data().commanderBlock.descriptionSeed0);
  GenerateSystemName(_state);
  CopySelectedNameLower(_state);
  CopyBytes(saved, selected);
  return ExpandDescriptionText(_state, _state.TextAt(_state.Data().descriptionNameBuffer), _output);
}

std::uint16_t BackspaceDescription(std::uint16_t _output)
{
  return static_cast<std::uint16_t>(_output - 1);
}

void StartCapitalizing(GameState& _state)
{
  _state.Data().descriptionCapitalize = 1;
}

void StopCapitalizing(GameState& _state)
{
  _state.Data().descriptionCapitalize = 0;
}

std::uint16_t NextDescriptionRandom(GameState& _state)
{
  const std::uint16_t a = _state.Data().commanderBlock.descriptionSeed0;
  const std::uint16_t b = _state.Data().commanderBlock.descriptionSeed1;
  const auto sum = static_cast<std::uint16_t>(a + b);
  _state.Data().commanderBlock.descriptionSeed0 = b;
  _state.Data().commanderBlock.descriptionSeed1 = sum;
  return sum;
}

std::size_t CopySelectedNameLower(GameState& _state)
{
  TerminateSelectedSystemName(_state);
  // The first byte whatever it is, then each up to the NUL, which is looked for from the second.
  const GameData::CommanderBlock::SelectedSystemName& selected = _state.Data().commanderBlock.selectedSystemName;
  const std::size_t length = 1 + _state.TextAt(selected, 1).size();
  const std::span<const Counted<std::uint8_t>> from = _state.BytesFrom(selected, length);
  const std::span<Counted<std::uint8_t>> to = _state.BytesFrom(_state.Data().descriptionNameBuffer, length + 2);
  to[0] = std::uint8_t{from[0]};
  for (std::size_t letter = 1; letter < length; ++letter)
  {
    to[letter] = static_cast<std::uint8_t>(from[letter] | LOWER_CASE_BIT);
  }
  // The space, then the NUL from AL.
  to[length] = SPACE;
  to[length + 1] = 0;
  return length;
}

} // namespace Elite
