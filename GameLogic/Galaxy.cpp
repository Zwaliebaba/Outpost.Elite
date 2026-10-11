#include "pch.h"

#include "Docked.h"
#include "Galaxy.h"

#include "Arithmetic.h"
#include "GalaxyTables.h"
#include "Input.h"
#include "Video.h"

#include <initializer_list>
#include <optional>
#include <utility>

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
// The ninth galaxy, galaxyNumber 8, and its seeds: the six bytes after galaxySeeds' eight entries, gap29A6, DS:29A6-29AB of the
// reference's image (GalaxyTests holds them to it), which the original reads as a ninth entry.
constexpr std::uint8_t NINTH_GALAXY = 8;
constexpr std::array<std::uint8_t, GALAXY_SEED_BYTES> NINTH_GALAXY_SEEDS = {0x34, 0x12, 0x78, 0x56, 0xBC, 0x9A};
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

// Where the data screen prints (text offsets), and the texts it writes into, in buffers the symbol table names no row for, by
// their bytes in SystemDataTexts and SystemSizeTexts; its labels are constants (GalaxyTables.h).
constexpr std::uint16_t DATA_TITLE = 0x0054;
constexpr std::uint16_t DISTANCE_ROW = 0x0144;
constexpr std::uint16_t ECONOMY_ROW = 0x01E4;
constexpr std::uint16_t GOVERNMENT_ROW = 0x0284;
constexpr std::uint16_t TECH_LEVEL_ROW = 0x0324;
constexpr std::size_t POPULATION_DIGITS = 0x35; // '000.0 Billion', DS:7D7D in 1987
constexpr std::size_t POPULATION_SHOWN = 2;     // the units digit, where the point goes after it
constexpr std::uint16_t POPULATION_ROW = 0x03C4;
constexpr std::uint16_t SPECIES_ROW = 0x0414;
constexpr std::uint8_t NO_SPECIES = 0xFF;
constexpr std::uint16_t PRODUCTIVITY_ROW = 0x04B4;
constexpr std::size_t RADIUS_TEXT = 0x0B;   // 'Radius (Av): 00000 km', DS:7EAE in 1987
constexpr std::size_t RADIUS_DIGITS = 0x18; // its digits, data7EBB on
constexpr std::uint16_t RADIUS_ROW = 0x0554;
constexpr std::uint8_t TECH_LEVEL_TENS = 10;

// FormatSelectedSystemDistance's text ends here, its tenths digit last: DS:7D5F in 1987, in SystemDataTexts.
constexpr std::size_t SELECTED_DISTANCE_TEXT_END = 0x17;
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
// The byte before descriptionBuffer in 1987, which ExpandDescriptionText reads before the first character it writes: the NUL that
// ends descriptionTemplate.
constexpr char BEFORE_DESCRIPTION_BUFFER = DESCRIPTION_TEMPLATE.data()[DESCRIPTION_TEMPLATE.size()];
static_assert(BEFORE_DESCRIPTION_BUFFER == '\0');

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

// CopyBytes from a constant's table (D38).
void CopyBytes(std::span<const std::uint8_t> _from, std::span<Counted<std::uint8_t>> _to)
{
  for (std::size_t done = 0; done < _from.size(); ++done)
  {
    _to[done] = _from[done];
  }
}

// selectedSystemName's eight bytes and the byte after them in the commander record, gap75CC: a name of up to eight letters and its
// NUL, which TerminateSelectedSystemName writes there after an eighth letter. The original reads the name as a text from its first
// byte, on past the eighth to a NUL; gap75CC is 0 in the image and in every commander the game writes, and a NUL past it throws
// std::out_of_range (TextWithin).
static_assert(GAME_DATA_OFFSET(commander.gap75CC) ==
              GAME_DATA_OFFSET(commander.selectedSystemName) + sizeof(CommanderState::SelectedSystemName));

template <typename State> [[nodiscard]] auto SystemNameField(State& _state)
{
  auto& commander = _state.Data().commander;
  return BytesThrough(commander.selectedSystemName, commander.gap75CC);
}

// The data screen's texts the symbol table names no row for, which ShowSystemDataScreen and FormatSelectedSystemDistance write
// digits into and print, members that follow one another in GalaxyState as in 1987 (change 3c names them): the tech level's two
// characters and their NUL, the distance's digits and the population's text, in the 86 bytes from data7D48 to gap7D4A's end; and the
// productivity's text and the radius's, its digits at data7EBB, in the 33 from data7EA3 to gap7EBC's end.
static_assert(GAME_DATA_OFFSET(galaxy.data7D49) == GAME_DATA_OFFSET(galaxy.data7D48) + 1 &&
              GAME_DATA_OFFSET(galaxy.gap7D4A) == GAME_DATA_OFFSET(galaxy.data7D49) + 1);
static_assert(GAME_DATA_OFFSET(galaxy.gap7EA4) == GAME_DATA_OFFSET(galaxy.data7EA3) + 1 &&
              GAME_DATA_OFFSET(galaxy.data7EBB) == GAME_DATA_OFFSET(galaxy.gap7EA4) + sizeof(GalaxyState::gap7EA4) &&
              GAME_DATA_OFFSET(galaxy.gap7EBC) == GAME_DATA_OFFSET(galaxy.data7EBB) + 1);
static_assert(RADIUS_DIGITS == GAME_DATA_OFFSET(galaxy.data7EBB) - GAME_DATA_OFFSET(galaxy.data7EA3));

[[nodiscard]] std::span<Counted<std::uint8_t>> SystemDataTexts(GameState& _state)
{
  GalaxyState& galaxy = _state.Data().galaxy;
  return BytesThrough(galaxy.data7D48, galaxy.gap7D4A);
}

[[nodiscard]] std::span<Counted<std::uint8_t>> SystemSizeTexts(GameState& _state)
{
  GalaxyState& galaxy = _state.Data().galaxy;
  return BytesThrough(galaxy.data7EA3, galaxy.gap7EBC);
}

// Item _index of chartItems, the item chartItemEnd numbers, as its eight bytes; and _count bytes of pendingChartLabels from its byte
// _index, where chartLabelCursor and a label's item number it. The original bounds neither: a short-range chart of more than 16
// systems writes its 33rd item over chartLabelCursor, then reads its labels wherever the cursor then points, a label's x range taken
// for an index. The GameState refused what followed: the 92 charts of more than 16 systems of the 2,304, every system of the nine
// galaxies as the current one, all stopped with std::out_of_range, at the reads past the labels it held together, and no chart that
// went on placed more than 32 items or 198 bytes of labels. This stops them at the 33rd item, or a label's bytes past
// pendingChartLabels.
template <typename State> [[nodiscard]] auto ChartItem(State& _state, std::size_t _index)
{
  return BytesWithin(BytesOf(_state.Data().galaxy.chartItems), _index * CHART_ITEM_BYTES, CHART_ITEM_BYTES);
}

template <typename State> [[nodiscard]] auto LabelBytes(State& _state, std::size_t _index, std::size_t _count)
{
  return BytesWithin(std::span(_state.Data().galaxy.pendingChartLabels), _index, _count);
}

// The text of a label in pendingChartLabels from its byte _index, up to its NUL, as ChartItem and LabelBytes bound it.
[[nodiscard]] std::string_view LabelText(const GameState& _state, std::size_t _index)
{
  return TextWithin(_state.Data().galaxy.pendingChartLabels, _index);
}

// One pass of PlaceChartLabels over chartItems (CS:152B): whether an item overlaps the label with x range _x and
// rows _rows. It stops at the first that does, and LOOP counts chartItemCount items, 65,536 for 0.
[[nodiscard]] bool AnyChartItemOverlaps(const GameState& _state, ChartSpan _x, ChartSpan _rows)
{
  std::size_t item = 0;
  std::uint16_t itemsLeft = _state.Data().galaxy.chartItemCount;
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

// The chart cursor's cross in colour 1 and the dot at its centre in colour 0 (CS:0D4D, CS:0F99).
void DrawChartCursor(GameState& _state)
{
  _state.Data().drawing.drawColor = 1;
  (void)DrawCross(_state, _state.Data().commander.chartCursorX, _state.Data().commander.chartCursorY, CURSOR_ARM);
  _state.Data().drawing.drawColor = 0;
  PlotPixel(_state, _state.Data().commander.chartCursorX, _state.Data().commander.chartCursorY);
}

// The steering moves a chart's cursor (CS:0DB4, CS:0FDA): the roll across, clamped to the chart, and the pitch negated down,
// clamped to its 128 rows. Returns the cursor, which the original leaves in AX.
ChartPoint MoveChartCursor(GameState& _state, Hardware& _hardware)
{
  const Steering steering = ReadSteering(_state, _hardware);
  const std::uint8_t across = steering.roll;
  const unsigned x = unsigned{across} + unsigned{_state.Data().commander.chartCursorX};
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
  _state.Data().commander.chartCursorX = cursor.x;
  const std::uint8_t down = Negate(steering.pitch);
  const unsigned y = unsigned{down} + unsigned{_state.Data().commander.chartCursorY};
  if ((down & 0x80) == 0)
  {
    const auto row = static_cast<std::uint8_t>(y);
    cursor.row = row < CHART_ROWS ? row : CURSOR_LOWEST_ROW;
  }
  else
  {
    cursor.row = y > 0xFF ? static_cast<std::uint8_t>(y) : std::uint8_t{0};
  }
  _state.Data().commander.chartCursorY = cursor.row;
  return cursor;
}

// One frame of the galactic chart up to its presentation (CS:0CF9): the fuel range about the current system, its cross, the
// cursor, and a dot for each of the galaxy's 256 systems, the seeds twisted four times from one to the next. _backward is the
// direction flag, which DrawDisc goes by. The dots' loop carries its count, in AL, and reports its turns to _hardware.
void DrawGalacticChart(GameState& _state, Hardware& _hardware, bool _backward)
{
  LoadGalaxySeeds(_state);
  const auto fuelRadius = static_cast<std::uint16_t>((_state.Data().commander.fuel >> 3) + 3);
  _state.Data().drawing.drawColor = 2;
  DrawDisc(_state, fuelRadius, _state.Data().commander.currentSystemX, _state.Data().commander.currentSystemChartY, _backward);
  _state.Data().drawing.drawColor = 0;
  (void)DrawCross(_state, _state.Data().commander.currentSystemX, _state.Data().commander.currentSystemChartY, CURRENT_SYSTEM_ARM);
  DrawChartCursor(_state);
  _state.Data().drawing.drawColor = CHART_DISC_COLOR;
  // XOR AL,AL, then PUSH AX / POP AX round each dot.
  for (std::uint8_t systems = 0;;)
  {
    PlotPixel(_state, _state.Data().galaxy.systemSeed1Bytes.systemX,
              static_cast<std::uint8_t>(_state.Data().galaxy.systemSeed0Bytes.systemY >> 1));
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
  const auto fuelRadius = static_cast<std::uint16_t>(_state.Data().commander.fuel >> 1);
  _state.Data().drawing.drawColor = 2;
  DrawDisc(_state, fuelRadius, SHORT_RANGE_CENTER_X, SHORT_RANGE_CENTER_ROW, _backward);
  _state.Data().drawing.drawColor = 0;
  // The current system's cross, from (50h, 51h) to (50h, 2Fh) and from (61h, 40h) to (3Fh, 40h).
  const bool downFilled = DrawLine(_state, 0x50, 0x51, 0x50, 0x2F);
  const bool acrossFilled = DrawLine(_state, 0x61, 0x40, 0x3F, 0x40);
  _state.Data().drawing.drawColor = CHART_DISC_COLOR;
  DrawChartItems(_state, _backward && !downFilled && !acrossFilled);
  DrawChartCursor(_state);
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
  bool recenterThroughAl;                             // the recentring goes through AL, and leaves the cursor's row there
  Counted<std::uint8_t> CommanderState::*keptCursorX; // where the cursor is kept while the chart is closed
  Counted<std::uint8_t> CommanderState::*keptCursorY;
};

// The galactic chart's cursor back on the current system (CS:0E19).
void RecenterGalacticCursor(GameState& _state)
{
  _state.Data().commander.chartCursorX = std::uint8_t{_state.Data().commander.currentSystemX};
  _state.Data().commander.chartCursorY = std::uint8_t{_state.Data().commander.currentSystemChartY};
}

// The short-range chart's cursor back on its centre (CS:103F).
void RecenterShortRangeCursor(GameState& _state)
{
  _state.Data().commander.chartCursorX = static_cast<std::uint8_t>(SHORT_RANGE_CENTER_X);
  _state.Data().commander.chartCursorY = static_cast<std::uint8_t>(SHORT_RANGE_CENTER_ROW);
}

constexpr ChartKeys GALACTIC_CHART_KEYS{.drawFrame = &DrawGalacticChart,
                                        .pacing = GALACTIC_CHART_PACING,
                                        .frame = GALACTIC_CHART_FRAME,
                                        .ignored = GALACTIC_KEY_IGNORED,
                                        .ownKey = SCAN_F5,
                                        .recenter = &RecenterGalacticCursor,
                                        .recenterThroughAl = true,
                                        .keptCursorX = &CommanderState::galacticCursorX,
                                        .keptCursorY = &CommanderState::galacticCursorY};
constexpr ChartKeys SHORT_RANGE_CHART_KEYS{.drawFrame = &DrawShortRangeChart,
                                           .pacing = SHORT_RANGE_CHART_PACING,
                                           .frame = SHORT_RANGE_CHART_FRAME,
                                           .ignored = SHORT_RANGE_KEY_IGNORED,
                                           .ownKey = SCAN_F6,
                                           .recenter = &RecenterShortRangeCursor,
                                           .recenterThroughAl = false,
                                           .keptCursorX = &CommanderState::shortRangeCursorX,
                                           .keptCursorY = &CommanderState::shortRangeCursorY};

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
      al = _state.Data().commander.chartCursorY;
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
  if (_state.Data().jump.hyperspaceCountdown == 0)
  {
    countLeft = SelectSystemAtCursor(_state, PRESENT_CHART_FRAME_BP);
    CommanderState& commander = _state.Data().commander;
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
  _state.Data().drawing.textPaperPattern = 0;
  (void)DrawScreenString(_state, _title, INK_1, _segment, CHART_TITLE);
  ClearChartTextLines(_state, INK_1, _segment);
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
  const std::uint16_t itemIndex = _state.Data().galaxy.chartItemEnd;
  const std::span<Counted<std::uint8_t>> item = ChartItem(_state, itemIndex);
  SetWordIn(item, CHART_ITEM_TEXT, Join(centerRow, centerX));
  const auto radius = static_cast<std::uint16_t>(((_state.Data().galaxy.systemSeed0Bytes.systemY & 1) << 1) + 4);
  SetWordIn(item, CHART_ITEM_RADIUS, radius);
  // Its box: half the radius either side, as add al,dl / xchg / sub al,dl / neg al make it.
  const auto half = static_cast<std::uint8_t>(radius >> 1);
  std::size_t at = CHART_ITEM_X;
  for (const std::uint8_t center : {centerX, centerRow})
  {
    SetWordIn(item, at, Join(static_cast<std::uint8_t>(half + center), static_cast<std::uint8_t>(center - half)));
    at += 2;
  }
  _state.Data().galaxy.chartItemEnd = static_cast<std::uint16_t>(itemIndex + 1);
  _state.Data().galaxy.chartItemCount = static_cast<std::uint8_t>(_state.Data().galaxy.chartItemCount + 1);
  const std::uint8_t length = GenerateSystemName(_state);

  // The label: from seven pixels right of the disc's centre, six a letter, and from two rows above it to three below.
  const auto left = static_cast<std::uint8_t>(centerX + LABEL_OFFSET_X);
  const ChartSpan x{left, static_cast<std::uint8_t>(left + Low(static_cast<std::uint16_t>(SMALL_LETTER_PIXELS * length)))};
  // The label's bytes, its name LOOP counts in CX, 65,536 letters for 0, and its NUL.
  const std::uint16_t cursor = _state.Data().galaxy.chartLabelCursor;
  const std::uint32_t letters = LoopCount(length);
  const std::span<const Counted<std::uint8_t>> name = BytesWithin(SystemNameField(std::as_const(_state)), 0, letters);
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
  // DI, as the bytes they point at, the name's and pendingChartLabels', and the count. A place is the original's address less its
  // buffer's, the same in every turn, so that a turn's signature equals the last one's exactly where the original's did.
  for (std::uint32_t copied = 0;;)
  {
    label[CHART_LABEL_NAME + copied] = std::uint8_t{name[copied]};
    ++copied;
    if (copied == letters)
    {
      break;
    }
    _hardware.LoopTurn(SHORT_RANGE_LABEL_NAME,
                       {static_cast<std::uint16_t>(copied), static_cast<std::uint16_t>(cursor + CHART_LABEL_NAME + copied),
                        static_cast<std::uint16_t>(letters - copied)});
  }
  label[CHART_LABEL_NAME + letters] = 0;
  _state.Data().galaxy.chartLabelCursor = static_cast<std::uint16_t>(cursor + CHART_LABEL_NAME + letters + 1);
}

// What REPE CMPSB finds.
struct ByteComparison
{
  std::uint16_t compared; // the bytes it compared, which SI and DI move on by and CX goes down by
  bool equal;             // every byte compared was equal: ZF
};

// REPE CMPSB with ES on the data segment: _source with _destination from their first bytes, for at most _count bytes, backwards when
// _backwards, until two differ. A count of 0 compares nothing, and the flags keep what they held. Its one use compares a name with
// what was typed, forwards (FindSystemByName); a byte before either, where the original would compare on backwards, throws
// std::out_of_range.
[[nodiscard]] ByteComparison CompareBytes(std::span<const Counted<std::uint8_t>> _source,
                                          std::span<const Counted<std::uint8_t>> _destination, std::uint16_t _count, bool _backwards)
{
  const std::size_t step = _backwards ? SIZE_MAX : 1;
  ByteComparison comparison{0, true};
  std::size_t source = 0;
  std::size_t destination = 0;
  while (comparison.compared < _count && comparison.equal)
  {
    const std::uint8_t sourceByte = ByteWithin(_source, source);
    const std::uint8_t destinationByte = ByteWithin(_destination, destination);
    comparison.equal = sourceByte == destinationByte;
    source += step;
    destination += step;
    ++comparison.compared;
  }
  return comparison;
}

// FindSystemByName's 'ERROR: <name> not on map!' on the second line under the chart (CS:148B), at _segment: the name as typed
// between the other two parts.
void ShowNotOnMap(GameState& _state, std::uint16_t _segment)
{
  const std::uint16_t prefixEnd = DrawScreenString(_state, FIND_ERROR_PREFIX, INK_3, _segment, CHART_TEXT_LINE_2);
  const std::uint16_t nameEnd = DrawScreenString(_state, TextWithin(_state.Data().galaxy.findInputEcho, 0), INK_2, _segment, prefixEnd);
  (void)DrawScreenString(_state, FIND_ERROR_SUFFIX, INK_3, _segment, nameEnd);
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
    const std::uint16_t across =
      Square(Low(Magnitude(static_cast<std::uint16_t>(_state.Data().galaxy.systemSeed1Bytes.systemX - cursor.x))));
    const std::uint16_t down =
      Square(Low(Magnitude(static_cast<std::uint16_t>((_state.Data().galaxy.systemSeed0Bytes.systemY >> 1) - cursor.row))));
    const std::uint32_t distance = std::uint32_t{across} + down;
    if (distance <= 0xFFFF && distance < nearest && IsSystemOnChart(_state))
    {
      nearest = static_cast<std::uint16_t>(distance);
      nearestCount = Low(count);
    }
    AdvanceToNextSystem(_state);
  }
  const auto index = static_cast<std::uint8_t>(GALAXY_SYSTEMS - nearestCount);
  _state.Data().commander.selectedSystemIndex = index;
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
  const std::span<Counted<std::uint8_t>> digits = BytesOf(_state.Data().galaxy.distanceDigits);
  FormatDecimal5(_state.Data().commander.selectedDistanceTenthsLy, digits);
  BlankLeadingZeros(digits, DISTANCE_ZEROS_BLANKED);
  return countLeft;
}

} // namespace

ScreenKey ShowGalacticChart(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _segment)
{
  _state.Data().drawing.sunFringeMask = 0;
  LoadSystemSeeds(_state, _state.Data().commander.currentSystemIndex);
  _state.Data().commander.currentSystemX = std::uint8_t{_state.Data().galaxy.systemSeed1Bytes.systemX};
  _state.Data().commander.currentSystemChartY = static_cast<std::uint8_t>(_state.Data().galaxy.systemSeed0Bytes.systemY >> 1);
  const std::uint16_t segment = ChartSegment(DrawChartFrame(_state, _hardware, _backward), _segment);
  _state.Data().galaxy.galacticChartNumber = static_cast<std::uint8_t>(_state.Data().commander.galaxyNumber + '1');
  // 'GALACTIC CHART ', the galaxy's digit, and a space and the NUL in gap2A7F after it.
  GalaxyState& galaxy = _state.Data().galaxy;
  static_assert(GAME_DATA_OFFSET(galaxy.galacticChartNumber) ==
                  GAME_DATA_OFFSET(galaxy.galacticChartTitle) + sizeof(GalaxyState::galacticChartTitle) &&
                GAME_DATA_OFFSET(galaxy.gap2A7F) == GAME_DATA_OFFSET(galaxy.galacticChartNumber) + 1);
  DrawChartTitle(_state, TextWithin(BytesThrough(galaxy.galacticChartTitle, galaxy.gap2A7F), 0), segment);
  _state.Data().commander.chartCursorX = std::uint8_t{_state.Data().commander.galacticCursorX};
  _state.Data().commander.chartCursorY = std::uint8_t{_state.Data().commander.galacticCursorY};
  _state.Data().commander.chartIsShortRange = 0;
  return RunChartFrames(_state, _hardware, GALACTIC_CHART_KEYS, _backward);
}

ScreenKey ShowShortRangeChart(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _segment)
{
  _state.Data().drawing.sunFringeMask = 0;
  const std::uint16_t segment = ChartSegment(DrawChartFrame(_state, _hardware, _backward), _segment);
  DrawChartTitle(_state, SHORT_RANGE_CHART_TITLE, segment);
  _state.Data().commander.chartCursorX = std::uint8_t{_state.Data().commander.shortRangeCursorX};
  _state.Data().commander.chartCursorY = std::uint8_t{_state.Data().commander.shortRangeCursorY};
  _state.Data().commander.chartIsShortRange = 1;
  _state.Data().galaxy.chartItemCount = 0;
  _state.Data().galaxy.chartItemEnd = 0;
  _state.Data().galaxy.chartLabelCursor = 0;
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
  return SystemSeeds{_state.Data().galaxy.systemSeed0, _state.Data().galaxy.systemSeed1, _state.Data().galaxy.systemSeed2};
}

ShortRangeOffset GetShortRangeOffset(const GameState& _state)
{
  if (_state.Data().commander.chartIsShortRange == 0)
  {
    return ShortRangeOffset{.onChart = true, .x = std::nullopt, .row = std::nullopt};
  }
  const SystemSeeds seeds = ReadSystemSeeds(_state);
  const auto x = static_cast<std::int16_t>(seeds.X() - _state.Data().commander.currentSystemX);
  if (Magnitude(Word(x)) >= SHORT_RANGE_HALF_WIDTH)
  {
    return ShortRangeOffset{.onChart = false, .x = x, .row = std::nullopt};
  }
  const auto row = static_cast<std::int16_t>(seeds.Row() - _state.Data().commander.currentSystemChartY);
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
  _state.Data().galaxy.systemSeed1 = seeds.seed2;
  _state.Data().galaxy.systemSeed0 = seeds.seed1;
  _state.Data().galaxy.systemSeed2 = static_cast<std::uint16_t>(seeds.seed0 + seeds.seed1 + seeds.seed2);
}

void LoadGalaxySeeds(GameState& _state)
{
  // The table offset is figured in BL, a byte: galaxyNumber times six, from galaxySeeds. The eight galaxies' seeds are its entries,
  // and the ninth's, which GalacticJump makes now and then after the eighth, the six bytes after it, gap29A6's (NINTH_GALAXY_SEEDS).
  // galaxyNumber is 0-8 wherever the game writes it (GalacticJump, a new game, a commander the game saved); a number past 8 would
  // have the original read the seeds on past gap29A6, and throws std::out_of_range.
  const std::uint8_t galaxy = _state.Data().commander.galaxyNumber;
  const std::span<const std::uint8_t> seeds =
    galaxy == NINTH_GALAXY ? std::span<const std::uint8_t>(NINTH_GALAXY_SEEDS) : std::span<const std::uint8_t>(GALAXY_SEEDS.at(galaxy));
  _state.Data().galaxy.systemSeed0 = WordIn(seeds, 0);
  _state.Data().galaxy.systemSeed1 = WordIn(seeds, 2);
  _state.Data().galaxy.systemSeed2 = WordIn(seeds, 4);
}

ChartPoint GetCursorGalaxyPosition(const GameState& _state)
{
  const ChartPoint cursor{_state.Data().commander.chartCursorX, _state.Data().commander.chartCursorY};
  if (_state.Data().commander.chartIsShortRange != 1)
  {
    return cursor;
  }
  const std::uint16_t x =
    ChartToGalaxy(static_cast<std::uint16_t>(cursor.x - SHORT_RANGE_CENTER_X), _state.Data().commander.currentSystemX);
  const std::uint16_t row =
    ChartToGalaxy(static_cast<std::uint16_t>(cursor.row - SHORT_RANGE_CENTER_ROW), _state.Data().commander.currentSystemChartY);
  return ChartPoint{Low(x), Low(row)};
}

void MoveCursorToSystem(GameState& _state)
{
  const SystemSeeds seeds = ReadSystemSeeds(_state);
  if (_state.Data().commander.chartIsShortRange != 1)
  {
    _state.Data().commander.chartCursorX = seeds.X();
    _state.Data().commander.chartCursorY = seeds.Row();
    return;
  }
  const auto across = static_cast<std::uint16_t>(seeds.X() - _state.Data().commander.currentSystemX);
  _state.Data().commander.chartCursorX = Low(GalaxyToChart(across, SHORT_RANGE_CENTER_X));
  const auto down = static_cast<std::uint16_t>(seeds.Row() - _state.Data().commander.currentSystemChartY);
  _state.Data().commander.chartCursorY = Low(GalaxyToChart(down, SHORT_RANGE_CENTER_ROW));
}

std::uint16_t SelectSystemAtCursor(GameState& _state, std::uint16_t _countIfNone)
{
  const std::uint16_t countLeft = SelectNearestSystem(_state, _countIfNone);
  DescribeSystem(_state);
  return countLeft;
}

void DescribeSystem(GameState& _state)
{
  const std::uint8_t seed1 = Low(_state.Data().galaxy.systemSeed1);
  const std::uint8_t seed2 = Low(_state.Data().galaxy.systemSeed2);
  const std::uint8_t seed2High = _state.Data().galaxy.systemSeed2Bytes.systemSeed2High;
  const std::uint8_t x = _state.Data().galaxy.systemSeed1Bytes.systemX;
  const std::uint8_t y = _state.Data().galaxy.systemSeed0Bytes.systemY;

  const auto government = static_cast<std::uint8_t>((seed1 >> 3) & 7);
  _state.Data().commander.selectedGovernment = government;
  // Governments 0 and 1 set bit 1 of the economy before it is inverted.
  const auto economy = static_cast<std::uint8_t>((((government >> 1) == 0 ? 2 : 0) | (y & 7)) ^ 7);
  _state.Data().commander.selectedEconomy = economy;
  const auto techLevel = static_cast<std::uint8_t>(((economy + 3) & x) + (seed2 & 1));
  _state.Data().commander.selectedTechLevel = techLevel;
  const auto population =
    static_cast<std::uint8_t>((Low(static_cast<std::uint16_t>(techLevel * economy)) >> 1) + 0x14 + ((seed1 >> 3) & 7));
  _state.Data().commander.selectedPopulationTenthsBillion = population;

  if ((seed2 & 0x80) == 0)
  {
    _state.Data().commander.selectedSpeciesAdjective1 = 0xFF;
  }
  else
  {
    const auto mixed = static_cast<std::uint8_t>((x ^ y) & 7);
    _state.Data().commander.selectedSpeciesAdjective1 = static_cast<std::uint8_t>((seed2High >> 2) & 7);
    _state.Data().commander.selectedSpeciesAdjective2 = static_cast<std::uint8_t>(seed2High >> 5);
    _state.Data().commander.selectedSpeciesAdjective3 = mixed;
    _state.Data().commander.selectedSpeciesType = static_cast<std::uint8_t>(((seed2High & 3) + mixed) & 7);
  }

  // (government+8)^2 in AL, times the population, times 4.
  const std::uint8_t squared = Low(Square(static_cast<std::uint8_t>(government + 8)));
  _state.Data().commander.selectedProductivityMillionCredits = static_cast<std::uint16_t>((squared * population) << 2);

  const std::uint16_t rows = MakeWord(y, y);
  const auto rotated = static_cast<std::uint16_t>((rows << 2) | (rows >> 14));
  const std::uint16_t seed2Word = _state.Data().galaxy.systemSeed2;
  const auto swapped = static_cast<std::uint16_t>(((seed2Word >> 8) | (seed2Word << 8)) & 0x3FF);
  _state.Data().commander.selectedRadiusKm = static_cast<std::uint16_t>(((rotated ^ swapped) & 0xFFF) + 0x10E1);

  const auto descriptionSeed0 = static_cast<std::uint16_t>(_state.Data().galaxy.systemSeed0 ^ _state.Data().galaxy.systemSeed1);
  _state.Data().commander.descriptionSeed0 = descriptionSeed0;
  _state.Data().commander.descriptionSeed1 = static_cast<std::uint16_t>(descriptionSeed0 ^ _state.Data().galaxy.systemSeed2);
  GenerateSystemName(_state);
}

std::uint8_t FindNearestSystem(GameState& _state, std::uint16_t _countIfNone)
{
  return static_cast<std::uint8_t>(GALAXY_SYSTEMS - SearchNearestSystem(_state, _countIfNone));
}

void ComputeDistanceToSystem(GameState& _state)
{
  const SystemSeeds seeds = ReadSystemSeeds(_state);
  // Each difference made positive, and its low byte squared by a byte multiply.
  const std::uint16_t across = Square(Low(Magnitude(static_cast<std::uint16_t>(seeds.X() - _state.Data().commander.currentSystemX))));
  const std::uint16_t down = Square(Low(Magnitude(static_cast<std::uint16_t>(seeds.Row() - _state.Data().commander.currentSystemChartY))));
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
  _state.Data().commander.selectedDistanceTenthsLy = distanceTenthsLy;
}

void ShowNearestSystemDistance(GameState& _state, std::uint16_t _countIfNone, std::uint16_t _segment)
{
  (void)SelectNearestSystem(_state, _countIfNone);
  // The distance's four digits into distanceText, either side of its point.
  constexpr std::array<std::size_t, 4> PLACES = {DISTANCE_TEXT_HUNDREDS, DISTANCE_TEXT_TENS, DISTANCE_TEXT_UNITS, DISTANCE_TEXT_TENTHS};
  constexpr std::array<Counted<std::uint8_t> GalaxyState::DistanceDigits::*, 4> DIGITS = {
    &GalaxyState::DistanceDigits::distanceHundredsDigit, &GalaxyState::DistanceDigits::distanceTensDigit,
    &GalaxyState::DistanceDigits::distanceUnitsDigit, &GalaxyState::DistanceDigits::distanceTenthsDigit};
  for (std::size_t digit = 0; digit < DIGITS.size(); ++digit)
  {
    _state.Data().galaxy.distanceText[PLACES[digit]] = std::uint8_t{_state.Data().galaxy.distanceDigits.*DIGITS[digit]};
  }
  _state.Data().drawing.textPaperPattern = 0;
  DrawScreenString(_state, TextWithin(_state.Data().galaxy.distanceText, 0), INK_3, _segment, CHART_TEXT_LINE_2);
  GenerateSystemName(_state);
  DrawScreenString(_state, TextWithin(SystemNameField(_state), 0), INK_2, _segment, CHART_TEXT_LINE_1);
  DrawScreenString(_state, NAME_LINE_PADDING, INK_2, _segment, CHART_NAME_PADDING);
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
  _state.Data().commander.selectedSystemName.data75CA = 0x2020;
  _state.Data().commander.selectedSystemName.data75C8 = 0x2020;
  // The fourth pair only when bit 6 of systemSeed0 was set.
  const bool fourPairs = (_state.Data().galaxy.systemSeed0 & SYSTEM_NAME_FOURTH_PAIR) != 0;
  const std::span<Counted<std::uint8_t>> name = BytesOf(_state.Data().commander.selectedSystemName);
  std::size_t at = 0;
  std::uint8_t length = 0;
  for (std::uint16_t pair = 0; pair < SYSTEM_NAME_PAIRS; ++pair)
  {
    const std::uint8_t pick = _state.Data().galaxy.systemSeed2Bytes.systemSeed2High;
    TwistSystemSeeds(_state);
    const std::array<std::uint8_t, 2>& letters = SYSTEM_NAME_DIGRAMS[pick & 0x1F];
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
  _state.Data().commander.selectedSystemNameLength = length;
  return length;
}

void FindSystemByName(GameState& _state, Hardware& _hardware, std::uint16_t _segment, std::uint16_t _countIfNone, bool _backward)
{
  LoadGalaxySeeds(_state);
  DrawScreenString(_state, FIND_PROMPT, INK_1, _segment, CHART_TEXT_LINE_1);
  DrawScreenString(_state, BLANK_CHART_LINE, INK_1, _segment, CHART_TEXT_LINE_2);
  const std::span<Counted<std::uint8_t>> typed(_state.Data().galaxy.findInput);
  for (std::size_t word = 0; word < SYSTEM_NAME_BYTES; word += 2)
  {
    SetWordIn(typed, word, 0);
  }
  ReadTextLine(_state, _hardware, typed, static_cast<std::uint8_t>(SYSTEM_NAME_BYTES), _segment, CHART_TEXT_LINE_2);
  if (typed[0] == 0)
  {
    // Nothing typed: on into ShowNearestSystemDistance, by a jump back to its entry, with ES as ReadTextLine leaves it.
    _hardware.LoopTurn(SHOW_NEAREST_SYSTEM_DISTANCE, {});
    const bool textLayout = _state.Data().drawing.screenLayout == TEXT_LAYOUT;
    ShowNearestSystemDistance(_state, _countIfNone, textLayout ? GameState::VIDEO_SEGMENT : _segment);
    return;
  }

  // The name echoed for the error message and upper-cased in place, a space after it; the count to compare its length,
  // counting the space when there is room for it. ReadTextLine leaves a NUL within findInput's nine bytes. Each turn of the loop
  // carries the two places, SI and DI, as the bytes of findInput and findInputEcho they point at, both the length, and the length.
  const std::span<Counted<std::uint8_t>> echo(_state.Data().galaxy.findInputEcho);
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
    _hardware.LoopTurn(FIND_UPPER_CASE, {length, length, length});
  }
  typed[length] = SPACE;
  const auto count = static_cast<std::uint16_t>(length == SYSTEM_NAME_BYTES ? length : length + 1);

  // Each system's name in turn, compared with it by REPE CMPSB in the data segment. Each turn carries the system and the count.
  for (std::uint8_t system = 0;;)
  {
    GenerateSystemName(_state);
    if (CompareBytes(SystemNameField(std::as_const(_state)), _state.Data().galaxy.findInput, count, _backward).equal)
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

void DrawChartItems(GameState& _state, bool _backward)
{
  // MOV CL,[chartItemCount] / AND CX,0FFh: CH stays 0, the high byte of a disc's centre row.
  const std::uint8_t items = _state.Data().galaxy.chartItemCount;
  if (items == 0)
  {
    return;
  }
  _state.Data().drawing.drawColor = CHART_DISC_COLOR;
  // PUSH CX / PUSH DI round each item keep the count and the item: the LOOP's count is a local.
  std::size_t index = 0;
  for (std::uint8_t itemsLeft = items; itemsLeft != 0; --itemsLeft)
  {
    const std::span<const Counted<std::uint8_t>> item = ChartItem(_state, index);
    const std::uint16_t mark = WordIn(item, CHART_ITEM_RADIUS);
    if (High(mark) != 0)
    {
      // A label, its text at its top-left corner: the x range's first byte in DL and the rows' first in DH.
      DrawSmallViewString(_state, LabelText(_state, WordIn(item, CHART_ITEM_TEXT)), INK_3,
                          MakeWord(item[CHART_ITEM_X], item[CHART_ITEM_ROWS]));
    }
    else
    {
      // A disc: the radius in BX, the centre's x in DX and its row in CX.
      const std::uint16_t center = WordIn(item, CHART_ITEM_TEXT);
      DrawDisc(_state, mark, Low(center), High(center), _backward);
    }
    ++index;
  }
}

void PlaceChartLabels(GameState& _state)
{
  const std::uint8_t labels = _state.Data().galaxy.chartItemCount;
  if (labels == 0)
  {
    return;
  }
  _state.Data().galaxy.labelsLeftToPlace = labels;
  _state.Data().galaxy.chartLabelCursor = 0;
  do
  {
    // The label's x range and rows, then its name.
    const std::uint16_t cursor = _state.Data().galaxy.chartLabelCursor;
    const std::span<const Counted<std::uint8_t>> label = LabelBytes(_state, cursor, CHART_LABEL_NAME);
    const ChartSpan x = SpanIn(label, CHART_ITEM_X);
    ChartSpan rows = SpanIn(label, CHART_ITEM_ROWS);
    _state.Data().galaxy.chartLabelCursor = static_cast<std::uint16_t>(cursor + 4);
    _state.Data().galaxy.labelNudgeCount = 0;
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
    _state.Data().galaxy.labelsLeftToPlace = static_cast<std::uint8_t>(_state.Data().galaxy.labelsLeftToPlace - 1);
  } while (_state.Data().galaxy.labelsLeftToPlace != 0);
}

void AddChartLabel(GameState& _state, ChartSpan _x, ChartSpan _rows)
{
  const std::uint16_t itemIndex = _state.Data().galaxy.chartItemEnd;
  const std::span<Counted<std::uint8_t>> item = ChartItem(_state, itemIndex);
  SetWordIn(item, CHART_ITEM_X, Word(_x));
  SetWordIn(item, CHART_ITEM_ROWS, Word(_rows));
  const std::uint16_t name = _state.Data().galaxy.chartLabelCursor;
  SetWordIn(item, CHART_ITEM_TEXT, name);
  item[CHART_ITEM_IS_LABEL] = 1;
  _state.Data().galaxy.chartItemEnd = static_cast<std::uint16_t>(itemIndex + 1);
  _state.Data().galaxy.chartItemCount = static_cast<std::uint8_t>(_state.Data().galaxy.chartItemCount + 1);
  // Past the name's NUL; the scan starts at its second byte.
  const std::size_t rest = LabelText(_state, std::size_t{name} + 1).size();
  _state.Data().galaxy.chartLabelCursor = static_cast<std::uint16_t>(name + 1 + rest + 1);
}

LabelNudge NudgeChartLabel(GameState& _state, ChartSpan _rows)
{
  ChartSpan rows = _rows;
  for (;;)
  {
    const auto step = static_cast<std::uint8_t>(_state.Data().galaxy.labelNudgeCount + 1);
    _state.Data().galaxy.labelNudgeCount = step;
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

void ClearChartTextLines(GameState& _state, std::uint16_t _ink, std::uint16_t _segment)
{
  DrawScreenString(_state, BLANK_CHART_LINE, _ink, _segment, CHART_TEXT_LINE_1);
  DrawScreenString(_state, BLANK_CHART_LINE, _ink, _segment, CHART_TEXT_LINE_2);
}

ScreenKey ShowSystemDataScreen(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _countIfNone)
{
  const std::uint16_t titleEnd = PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, SYSTEM_DATA_FRAME, _backward), DATA_TITLE);
  TerminateSelectedSystemName(_state);
  PrintTextModeString(_state, TextWithin(SystemNameField(_state), 0), titleEnd);
  std::uint16_t printed = PrintTextModeString(_state, DISTANCE_LABEL_TEXT, DISTANCE_ROW);
  printed = PrintTextModeString(_state, FormatSelectedSystemDistance(_state), printed);
  PrintTextModeString(_state, LIGHT_YEARS_TEXT, printed);
  printed = PrintTextModeString(_state, ECONOMY_LABEL_TEXT, ECONOMY_ROW);
  PrintTextModeString(_state, ECONOMY_NAMES[_state.Data().commander.selectedEconomy & SEVEN_NAMES], printed);
  printed = PrintTextModeString(_state, GOVERNMENT_LABEL_TEXT, GOVERNMENT_ROW);
  PrintTextModeString(_state, GOVERNMENT_NAMES[_state.Data().commander.selectedGovernment & SEVEN_NAMES], printed);
  printed = PrintTextModeString(_state, TECH_LEVEL_LABEL_TEXT, TECH_LEVEL_ROW);

  // The tech level, one to sixteen, in the two characters at data7D48, a column left when it is one digit.
  auto level = static_cast<std::uint8_t>(_state.Data().commander.selectedTechLevel + 1);
  auto cell = static_cast<std::uint16_t>(printed - 2);
  _state.Data().galaxy.data7D48 = SPACE;
  if (level >= TECH_LEVEL_TENS)
  {
    level = static_cast<std::uint8_t>(level - TECH_LEVEL_TENS);
    _state.Data().galaxy.data7D48 = '1';
    cell = Offset(cell, 2);
  }
  _state.Data().galaxy.data7D49 = static_cast<std::uint8_t>(level + '0');
  PrintTextModeString(_state, TextWithin(SystemDataTexts(_state), 0), cell);

  // The population in tenths of a billion, its last two digits about a point.
  const std::span<Counted<std::uint8_t>> population = BytesWithin(SystemDataTexts(_state), POPULATION_DIGITS, DECIMAL_DIGITS);
  FormatDecimal5(_state.Data().commander.selectedPopulationTenthsBillion, population);
  printed = PrintTextModeString(_state, POPULATION_LABEL_TEXT, POPULATION_ROW);
  population[POPULATION_SHOWN] = std::uint8_t{population[POPULATION_SHOWN + 1]};
  population[POPULATION_SHOWN + 1] = DECIMAL_POINT;
  PrintTextModeString(_state, TextWithin(SystemDataTexts(_state), POPULATION_DIGITS + POPULATION_SHOWN), printed);

  printed = PrintTextModeString(_state, SPECIES_OPEN_TEXT, SPECIES_ROW);
  const std::uint8_t size = _state.Data().commander.selectedSpeciesAdjective1;
  if (size == NO_SPECIES)
  {
    PrintTextModeString(_state, HUMAN_COLONIALS_TEXT, printed);
  }
  else
  {
    // Each of the species' four bytes indexes its table (SHL AL,1 / XOR AH,AH). DescribeSystem gives each three bits.
    const CommanderState& commander = _state.Data().commander;
    cell = PrintTextModeString(_state, SPECIES_SIZE_NAMES[size & SEVEN_NAMES], printed);
    cell = PrintTextModeString(_state, SPECIES_COLOR_NAMES[commander.selectedSpeciesAdjective2 & SEVEN_NAMES], cell);
    cell = PrintTextModeString(_state, SPECIES_TRAIT_NAMES[commander.selectedSpeciesAdjective3 & SEVEN_NAMES], cell);
    PrintTextModeString(_state, SPECIES_TYPE_NAMES[commander.selectedSpeciesType & SEVEN_NAMES], cell);
  }

  // The productivity, its leading zero blanked a column left; the radius, its leading digit blanked.
  FormatDecimal5(_state.Data().commander.selectedProductivityMillionCredits, BytesWithin(SystemSizeTexts(_state), 0, DECIMAL_DIGITS));
  cell = PrintTextModeString(_state, PRODUCTIVITY_LABEL_TEXT, PRODUCTIVITY_ROW);
  if (_state.Data().galaxy.data7EA3 == '0')
  {
    _state.Data().galaxy.data7EA3 = SPACE;
    cell = static_cast<std::uint16_t>(cell - 2);
  }
  PrintTextModeString(_state, TextWithin(SystemSizeTexts(_state), 0), cell);
  FormatDecimal5(_state.Data().commander.selectedRadiusKm, BytesWithin(SystemSizeTexts(_state), RADIUS_DIGITS, DECIMAL_DIGITS));
  _state.Data().galaxy.data7EBB = SPACE;
  PrintTextModeString(_state, TextWithin(SystemSizeTexts(_state), RADIUS_TEXT), RADIUS_ROW);
  ShowSystemDescription(_state, _backward);
  // ShowSystemDescription's last print leaves AL 0.
  return WaitForScreenExitKey(_state, _hardware, SCAN_F7, 0, _countIfNone);
}

void TerminateSelectedSystemName(GameState& _state)
{
  // GenerateSystemName's length, at most eight, wherever the game writes it: the NUL goes in the name field.
  const std::uint8_t length = _state.Data().commander.selectedSystemNameLength;
  ByteWithin(SystemNameField(_state), length) = 0;
}

std::string_view FormatSelectedSystemDistance(GameState& _state)
{
  // SI and DI, backwards: the copy runs until it has copied a space, which SelectNearestSystem leaves before the digits: the
  // distance is below 10000 tenths of a light year (it is at most four times the root of 255^2 + 127^2), so its first digit is a
  // zero BlankLeadingZeros blanks, and the copy stops there at the latest.
  const std::span<const Counted<std::uint8_t>> digits = BytesOf(_state.Data().galaxy.distanceDigits);
  const std::span<Counted<std::uint8_t>> texts = SystemDataTexts(_state);
  std::size_t from = digits.size() - 1;
  std::size_t to = SELECTED_DISTANCE_TEXT_END;
  ByteWithin(texts, to) = std::uint8_t{ByteWithin(digits, from)};
  --to;
  ByteWithin(texts, to) = DECIMAL_POINT;
  // Backwards through the digits, up to and including the space before them.
  std::uint8_t copied = 0;
  do
  {
    --to;
    --from;
    copied = ByteWithin(digits, from);
    ByteWithin(texts, to) = copied;
  } while (copied != SPACE);
  return TextWithin(texts, to + 1);
}

void ShowSystemDescription(GameState& _state, bool _backward)
{
  // REP STOSW of zeros over descriptionBuffer, ES = DS, forwards or, _backward, down from its start: DI, a byte of it. The
  // direction flag is clear wherever the original runs it, and a word down past its start throws std::out_of_range.
  const std::span<Counted<std::uint8_t>> buffer(_state.Data().galaxy.descriptionBuffer);
  std::uint16_t word = 0;
  for (std::uint16_t words = DESCRIPTION_BUFFER_WORDS; words != 0; --words)
  {
    SetWordIn(BytesWithin(buffer, word, 2), 0, 0);
    word = Offset(word, WordStep(_backward));
  }
  const std::uint16_t expanded = ExpandDescriptionText(_state, DESCRIPTION_TEMPLATE, 0);
  ByteWithin(buffer, expanded) = 0;

  // Word-wrapped at the last space within 36 characters, a text row at a time: SI the line and DI its end, bytes of the buffer.
  std::uint16_t line = 0;
  std::uint16_t cell = DESCRIPTION_SCREEN_OFFSET;
  for (;;)
  {
    std::uint16_t end = Offset(line, DESCRIPTION_LINE_CHARACTERS);
    if (ByteWithin(buffer, end) == 0)
    {
      (void)PrintTextModeString(_state, TextWithin(buffer, line), cell);
      return;
    }
    while (ByteWithin(buffer, end) != SPACE)
    {
      --end;
    }
    ByteWithin(buffer, end) = 0;
    const std::string_view text = TextWithin(buffer, line);
    (void)PrintTextModeString(_state, text, cell);
    line = Offset(line, static_cast<std::uint16_t>(text.size() + 1));
    cell = Offset(cell, TEXT_ROW_BYTES);
  }
}

std::uint16_t ExpandDescriptionText(GameState& _state, std::string_view _text, std::uint16_t _output)
{
  const std::span<Counted<std::uint8_t>> buffer(_state.Data().galaxy.descriptionBuffer);
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
    // The byte before the output, which the original reads at DI-1: before the buffer's first byte, the byte before it in 1987,
    // descriptionTemplate's NUL (DESCRIPTION_TEMPLATE's nine bytes end with it at DS:9662).
    const std::uint8_t previous = output == 0 ? std::uint8_t{BEFORE_DESCRIPTION_BUFFER} : std::uint8_t{ByteWithin(buffer, output - 1)};
    if (code == SPACE && previous == SPACE)
    {
      continue;
    }
    // Capitalizing reaches only 0x60-0x78: y and z stay lower case.
    std::uint8_t character = code;
    if (_state.Data().galaxy.descriptionCapitalize == 1 && previous == SPACE && code >= 0x60 && code < 0x79)
    {
      character = static_cast<std::uint8_t>(code & UPPER_CASE_MASK);
    }
    ByteWithin(buffer, output) = character;
    next = Offset(output, 1);
  }
  return next;
}

std::uint16_t InsertSystemName(GameState& _state, std::uint16_t _output)
{
  // MOV AX,20h / MOV [DI],AX: the space and the NUL after the name again, as a word.
  const std::size_t space = CopySelectedNameLower(_state);
  SetWordIn(BytesWithin(std::span(_state.Data().galaxy.descriptionNameBuffer), space, 2), 0, SPACE);
  return ExpandDescriptionText(_state, TextWithin(_state.Data().galaxy.descriptionNameBuffer, 0), _output);
}

std::uint16_t InsertSystemAdjective(GameState& _state, std::uint16_t _output)
{
  // The name has a first letter: CopySelectedNameLower copies it whatever it is.
  std::size_t suffix = CopySelectedNameLower(_state);
  const std::span<Counted<std::uint8_t>> name =
    BytesWithin(std::span(_state.Data().galaxy.descriptionNameBuffer), 0, suffix + ADJECTIVE_SUFFIX_BYTES);
  const std::uint8_t last = name[suffix - 1];
  if (last == 'a' || last == 'e' || last == 'i' || last == 'o' || last == 'u')
  {
    --suffix;
  }
  CopyBytes(ADJECTIVE_SUFFIX, name.subspan(suffix, ADJECTIVE_SUFFIX_BYTES));
  return ExpandDescriptionText(_state, TextWithin(_state.Data().galaxy.descriptionNameBuffer, 0), _output);
}

std::uint16_t InsertRandomName(GameState& _state, std::uint16_t _output)
{
  const std::span<Counted<std::uint8_t>> selected = BytesOf(_state.Data().commander.selectedSystemName);
  const std::span<Counted<std::uint8_t>> saved =
    std::span(_state.Data().galaxy.invasionDebriefText).subspan(SAVED_SYSTEM_NAME, SYSTEM_NAME_BYTES);
  CopyBytes(selected, saved);
  const std::uint16_t seed0 = _state.Data().commander.descriptionSeed0;
  _state.Data().galaxy.systemSeed0 = seed0;
  const std::uint16_t seed1 = _state.Data().commander.descriptionSeed1;
  _state.Data().galaxy.systemSeed1 = seed1;
  _state.Data().galaxy.systemSeed2 = static_cast<std::uint16_t>(seed1 ^ _state.Data().commander.descriptionSeed0);
  GenerateSystemName(_state);
  CopySelectedNameLower(_state);
  CopyBytes(saved, selected);
  return ExpandDescriptionText(_state, TextWithin(_state.Data().galaxy.descriptionNameBuffer, 0), _output);
}

std::uint16_t BackspaceDescription(std::uint16_t _output)
{
  return static_cast<std::uint16_t>(_output - 1);
}

void StartCapitalizing(GameState& _state)
{
  _state.Data().galaxy.descriptionCapitalize = 1;
}

void StopCapitalizing(GameState& _state)
{
  _state.Data().galaxy.descriptionCapitalize = 0;
}

std::uint16_t NextDescriptionRandom(GameState& _state)
{
  const std::uint16_t a = _state.Data().commander.descriptionSeed0;
  const std::uint16_t b = _state.Data().commander.descriptionSeed1;
  const auto sum = static_cast<std::uint16_t>(a + b);
  _state.Data().commander.descriptionSeed0 = b;
  _state.Data().commander.descriptionSeed1 = sum;
  return sum;
}

std::size_t CopySelectedNameLower(GameState& _state)
{
  TerminateSelectedSystemName(_state);
  // The first byte whatever it is, then each up to the NUL, which is looked for from the second.
  const std::span<const Counted<std::uint8_t>> selected = SystemNameField(std::as_const(_state));
  const std::size_t length = 1 + TextWithin(selected, 1).size();
  const std::span<const Counted<std::uint8_t>> from = BytesWithin(selected, 0, length);
  const std::span<Counted<std::uint8_t>> to = BytesWithin(std::span(_state.Data().galaxy.descriptionNameBuffer), 0, length + 2);
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
