#include "pch.h"

#include "Docked.h"
#include "Galaxy.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
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

// The control codes' handlers as textControlCodes holds them.
constexpr std::uint16_t INSERT_SYSTEM_NAME = 0x707A;
constexpr std::uint16_t INSERT_SYSTEM_ADJECTIVE = 0x708D;
constexpr std::uint16_t INSERT_RANDOM_NAME = 0x70C1;
constexpr std::uint16_t BACKSPACE_DESCRIPTION = 0x7107;
constexpr std::uint16_t START_CAPITALIZING = 0x7109;
constexpr std::uint16_t STOP_CAPITALIZING = 0x710F;

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
// then a label's text or a disc's centre, and a disc's radius or a label's mark in the last byte.
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
  _state.Set(DS.drawColor, 1);
  const bool filled = DrawCross(_state, _state.Get(DS.chartCursorX), _state.Get(DS.chartCursorY), CURSOR_ARM);
  _state.Set(DS.drawColor, 0);
  PlotPixel(_state, _state.Get(DS.chartCursorX), _state.Get(DS.chartCursorY));
  return filled;
}

// The steering moves a chart's cursor (CS:0DB4, CS:0FDA): the roll across, clamped to the chart, and the pitch negated down,
// clamped to its 128 rows. _trigger is the AL the chart's loop leaves, which ReadSteering fires the stick with. Returns the
// cursor, which the original leaves in AX.
ChartPoint MoveChartCursor(GameState& _state, Hardware& _hardware, std::uint8_t _trigger)
{
  const Steering steering = ReadSteering(_state, _hardware, _trigger);
  const std::uint8_t across = steering.roll;
  const unsigned x = unsigned{across} + unsigned{_state.Get(DS.chartCursorX)};
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
  _state.Set(DS.chartCursorX, cursor.x);
  const std::uint8_t down = Negate(steering.pitch);
  const unsigned y = unsigned{down} + unsigned{_state.Get(DS.chartCursorY)};
  if ((down & 0x80) == 0)
  {
    const auto row = static_cast<std::uint8_t>(y);
    cursor.row = row < CHART_ROWS ? row : CURSOR_LOWEST_ROW;
  }
  else
  {
    cursor.row = y > 0xFF ? static_cast<std::uint8_t>(y) : std::uint8_t{0};
  }
  _state.Set(DS.chartCursorY, cursor.row);
  return cursor;
}

// One frame of the galactic chart up to its presentation (CS:0CF9): the fuel range about the current system, its cross, the
// cursor, and a dot for each of the galaxy's 256 systems, the seeds twisted four times from one to the next. _backward is the
// direction flag, which DrawDisc goes by. The dots' loop carries its count, in AL, and reports its turns to _hardware.
void DrawGalacticChart(GameState& _state, Hardware& _hardware, bool _backward)
{
  LoadGalaxySeeds(_state);
  const auto fuelRadius = static_cast<std::uint16_t>((_state.Get(DS.fuel) >> 3) + 3);
  _state.Set(DS.drawColor, 2);
  DrawDisc(_state, fuelRadius, _state.Get(DS.currentSystemX), _state.Get(DS.currentSystemChartY), _backward);
  _state.Set(DS.drawColor, 0);
  (void)DrawCross(_state, _state.Get(DS.currentSystemX), _state.Get(DS.currentSystemChartY), CURRENT_SYSTEM_ARM);
  (void)DrawChartCursor(_state);
  _state.Set(DS.drawColor, CHART_DISC_COLOR);
  // XOR AL,AL, then PUSH AX / POP AX round each dot.
  for (std::uint8_t systems = 0;;)
  {
    PlotPixel(_state, _state.Get(DS.systemX), static_cast<std::uint8_t>(_state.Get(DS.systemY) >> 1));
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
  const auto fuelRadius = static_cast<std::uint16_t>(_state.Get(DS.fuel) >> 1);
  _state.Set(DS.drawColor, 2);
  DrawDisc(_state, fuelRadius, SHORT_RANGE_CENTER_X, SHORT_RANGE_CENTER_ROW, _backward);
  _state.Set(DS.drawColor, 0);
  // The current system's cross, from (50h, 51h) to (50h, 2Fh) and from (61h, 40h) to (3Fh, 40h).
  const bool downFilled = DrawLine(_state, 0x50, 0x51, 0x50, 0x2F);
  const bool acrossFilled = DrawLine(_state, 0x61, 0x40, 0x3F, 0x40);
  _state.Set(DS.drawColor, CHART_DISC_COLOR);
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

constexpr ChartKeys GALACTIC_CHART_KEYS{.drawFrame = &DrawGalacticChart,
                                        .pacing = GALACTIC_CHART_PACING,
                                        .frame = GALACTIC_CHART_FRAME,
                                        .ignored = GALACTIC_KEY_IGNORED,
                                        .ownKey = SCAN_F5,
                                        .recenter = &RecenterGalacticCursor,
                                        .recenterThroughAl = true,
                                        .keptCursorX = DS.galacticCursorX,
                                        .keptCursorY = DS.galacticCursorY};
constexpr ChartKeys SHORT_RANGE_CHART_KEYS{.drawFrame = &DrawShortRangeChart,
                                           .pacing = SHORT_RANGE_CHART_PACING,
                                           .frame = SHORT_RANGE_CHART_FRAME,
                                           .ignored = SHORT_RANGE_KEY_IGNORED,
                                           .ownKey = SCAN_F6,
                                           .recenter = &RecenterShortRangeCursor,
                                           .recenterThroughAl = false,
                                           .keptCursorX = DS.shortRangeCursorX,
                                           .keptCursorY = DS.shortRangeCursorY};

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
      al = _state.Get(DS.chartCursorY);
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
  if (_state.Get(DS.hyperspaceCountdown) == 0)
  {
    countLeft = SelectSystemAtCursor(_state, PRESENT_CHART_FRAME_BP);
    _state.Set(_chart.keptCursorX, _state.Get(DS.chartCursorX));
    _state.Set(_chart.keptCursorY, _state.Get(DS.chartCursorY));
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
    const ChartPoint cursor = MoveChartCursor(_state, _hardware, Low(GameState::VIDEO_SEGMENT));
    if (const std::optional<ScreenKey> key = ReadChartKey(_state, _hardware, _chart, cursor.x))
    {
      return *key;
    }
  }
}

// What both charts draw first: the title at DS:_title in colour 1 on no paper (textPaperPattern 0) at the top of the chart, and the
// two text lines under it cleared, at _segment, the screen as DrawChartFrame leaves ES.
void DrawChartTitle(GameState& _state, std::uint16_t _title, std::uint16_t _segment)
{
  _state.Set(DS.textPaperPattern, 0);
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
  std::uint16_t item = _state.Get(DS.chartItemEnd);
  _state.SetWord(Offset(item, CHART_ITEM_TEXT), Join(centerRow, centerX));
  const auto radius = static_cast<std::uint16_t>(((_state.Get(DS.systemY) & 1) << 1) + 4);
  _state.SetWord(Offset(item, CHART_ITEM_RADIUS), radius);
  // Its box: half the radius either side, as add al,dl / xchg / sub al,dl / neg al make it.
  const auto half = static_cast<std::uint8_t>(radius >> 1);
  for (const std::uint8_t center : {centerX, centerRow})
  {
    _state.SetWord(item, Join(static_cast<std::uint8_t>(half + center), static_cast<std::uint8_t>(center - half)));
    item = Offset(item, 2);
  }
  _state.Set(DS.chartItemEnd, Offset(item, CHART_ITEM_BYTES - 4));
  _state.Set(DS.chartItemCount, static_cast<std::uint8_t>(_state.Get(DS.chartItemCount) + 1));
  const std::uint8_t length = GenerateSystemName(_state);

  // The label: from seven pixels right of the disc's centre, six a letter, and from two rows above it to three below.
  const auto left = static_cast<std::uint8_t>(centerX + LABEL_OFFSET_X);
  const ChartSpan x{left, static_cast<std::uint8_t>(left + Low(static_cast<std::uint16_t>(SMALL_LETTER_PIXELS * length)))};
  const std::uint16_t label = _state.Get(DS.chartLabelCursor);
  _state.SetWord(label, Word(x));
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
  _state.SetWord(Offset(label, CHART_ITEM_ROWS), Word(rows));
  // The name, a byte at a time, LOOP counting its length in CX; then the NUL, from CH.
  std::uint16_t from = DS.selectedSystemName.offset;
  std::uint16_t to = Offset(label, CHART_ITEM_TEXT);
  std::uint16_t lettersLeft = length;
  for (;;)
  {
    _state.SetByte(to, _state.Byte(from));
    from = Offset(from, 1);
    to = Offset(to, 1);
    if (--lettersLeft == 0)
    {
      break;
    }
    _hardware.LoopTurn(SHORT_RANGE_LABEL_NAME, {from, to, lettersLeft});
  }
  _state.SetByte(to, 0);
  to = Offset(to, 1);
  _state.Set(DS.chartLabelCursor, to);
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

// PrintTextModeString, at B800:_cell, of the text whose pointer is _bytes into the table at DS:_table.
PrintedText PrintNamed(GameState& _state, std::uint16_t _table, std::uint16_t _bytes, std::uint16_t _cell)
{
  return PrintTextModeString(_state, _state.Word(Offset(_table, _bytes)), _cell);
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
  return nearestCount;
}

// What SelectSystemAtCursor and ShowNearestSystemDistance begin with: FindNearestSystem, ComputeDistanceToSystem, and the
// distance, read back from selectedDistanceTenthsLy, into distanceDigits with up to three leading zeros blanked. Returns the BP
// FindNearestSystem leaves.
std::uint16_t SelectNearestSystem(GameState& _state, std::uint16_t _countIfNone)
{
  const std::uint16_t countLeft = SearchNearestSystem(_state, _countIfNone);
  ComputeDistanceToSystem(_state);
  FormatDecimal5(_state, _state.Get(DS.selectedDistanceTenthsLy), DS.distanceDigits.offset);
  BlankLeadingZeros(_state, DS.distanceDigits.offset, DISTANCE_ZEROS_BLANKED);
  return countLeft;
}

} // namespace

ScreenKey ShowGalacticChart(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _segment)
{
  _state.Set(DS.sunFringeMask, 0);
  LoadSystemSeeds(_state, _state.Get(DS.currentSystemIndex));
  _state.Set(DS.currentSystemX, _state.Get(DS.systemX));
  _state.Set(DS.currentSystemChartY, static_cast<std::uint8_t>(_state.Get(DS.systemY) >> 1));
  const std::uint16_t segment = ChartSegment(DrawChartFrame(_state, _hardware, _backward), _segment);
  _state.Set(DS.galacticChartNumber, static_cast<std::uint8_t>(_state.Get(DS.galaxyNumber) + '1'));
  DrawChartTitle(_state, DS.galacticChartTitle.offset, segment);
  _state.Set(DS.chartCursorX, _state.Get(DS.galacticCursorX));
  _state.Set(DS.chartCursorY, _state.Get(DS.galacticCursorY));
  _state.Set(DS.chartIsShortRange, 0);
  return RunChartFrames(_state, _hardware, GALACTIC_CHART_KEYS, _backward);
}

ScreenKey ShowShortRangeChart(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _segment)
{
  _state.Set(DS.sunFringeMask, 0);
  const std::uint16_t segment = ChartSegment(DrawChartFrame(_state, _hardware, _backward), _segment);
  DrawChartTitle(_state, DS.shortRangeChartTitle.offset, segment);
  _state.Set(DS.chartCursorX, _state.Get(DS.shortRangeCursorX));
  _state.Set(DS.chartCursorY, _state.Get(DS.shortRangeCursorY));
  _state.Set(DS.chartIsShortRange, 1);
  _state.Set(DS.chartItemCount, 0);
  _state.Set(DS.chartItemEnd, DS.chartItems.offset);
  _state.Set(DS.chartLabelCursor, DS.pendingChartLabels.offset);
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

std::uint16_t SelectSystemAtCursor(GameState& _state, std::uint16_t _countIfNone)
{
  const std::uint16_t countLeft = SelectNearestSystem(_state, _countIfNone);

  const std::uint8_t seed1 = Low(_state.Get(DS.systemSeed1));
  const std::uint8_t seed2 = Low(_state.Get(DS.systemSeed2));
  const std::uint8_t seed2High = _state.Get(DS.systemSeed2High);
  const std::uint8_t x = _state.Get(DS.systemX);
  const std::uint8_t y = _state.Get(DS.systemY);

  const auto government = static_cast<std::uint8_t>((seed1 >> 3) & 7);
  _state.Set(DS.selectedGovernment, government);
  // Governments 0 and 1 set bit 1 of the economy before it is inverted.
  const auto economy = static_cast<std::uint8_t>((((government >> 1) == 0 ? 2 : 0) | (y & 7)) ^ 7);
  _state.Set(DS.selectedEconomy, economy);
  const auto techLevel = static_cast<std::uint8_t>(((economy + 3) & x) + (seed2 & 1));
  _state.Set(DS.selectedTechLevel, techLevel);
  const auto population =
    static_cast<std::uint8_t>((Low(static_cast<std::uint16_t>(techLevel * economy)) >> 1) + 0x14 + ((seed1 >> 3) & 7));
  _state.Set(DS.selectedPopulationTenthsBillion, population);

  if ((seed2 & 0x80) == 0)
  {
    _state.Set(DS.selectedSpeciesAdjective1, 0xFF);
  }
  else
  {
    const auto mixed = static_cast<std::uint8_t>((x ^ y) & 7);
    _state.Set(DS.selectedSpeciesAdjective1, static_cast<std::uint8_t>((seed2High >> 2) & 7));
    _state.Set(DS.selectedSpeciesAdjective2, static_cast<std::uint8_t>(seed2High >> 5));
    _state.Set(DS.selectedSpeciesAdjective3, mixed);
    _state.Set(DS.selectedSpeciesType, static_cast<std::uint8_t>(((seed2High & 3) + mixed) & 7));
  }

  // (government+8)^2 in AL, times the population, times 4.
  const std::uint8_t squared = Low(Square(static_cast<std::uint8_t>(government + 8)));
  _state.Set(DS.selectedProductivityMillionCredits, static_cast<std::uint16_t>((squared * population) << 2));

  const std::uint16_t rows = MakeWord(y, y);
  const auto rotated = static_cast<std::uint16_t>((rows << 2) | (rows >> 14));
  const std::uint16_t seed2Word = _state.Get(DS.systemSeed2);
  const auto swapped = static_cast<std::uint16_t>(((seed2Word >> 8) | (seed2Word << 8)) & 0x3FF);
  _state.Set(DS.selectedRadiusKm, static_cast<std::uint16_t>(((rotated ^ swapped) & 0xFFF) + 0x10E1));

  const auto descriptionSeed0 = static_cast<std::uint16_t>(_state.Get(DS.systemSeed0) ^ _state.Get(DS.systemSeed1));
  _state.Set(DS.descriptionSeed0, descriptionSeed0);
  _state.Set(DS.descriptionSeed1, static_cast<std::uint16_t>(descriptionSeed0 ^ _state.Get(DS.systemSeed2)));
  GenerateSystemName(_state);
  return countLeft;
}

std::uint8_t FindNearestSystem(GameState& _state, std::uint16_t _countIfNone)
{
  return static_cast<std::uint8_t>(GALAXY_SYSTEMS - SearchNearestSystem(_state, _countIfNone));
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

void ShowNearestSystemDistance(GameState& _state, std::uint16_t _countIfNone, std::uint16_t _segment)
{
  (void)SelectNearestSystem(_state, _countIfNone);
  // The distance's four digits into distanceText, either side of its point.
  constexpr std::array<std::uint16_t, 4> PLACES = {DISTANCE_TEXT_HUNDREDS, DISTANCE_TEXT_TENS, DISTANCE_TEXT_UNITS, DISTANCE_TEXT_TENTHS};
  constexpr std::array<DataField<std::uint8_t>, 4> DIGITS = {DS.distanceHundredsDigit, DS.distanceTensDigit, DS.distanceUnitsDigit,
                                                             DS.distanceTenthsDigit};
  for (std::size_t digit = 0; digit < DIGITS.size(); ++digit)
  {
    _state.SetByte(Offset(DS.distanceText.offset, PLACES[digit]), _state.Get(DIGITS[digit]));
  }
  _state.Set(DS.textPaperPattern, 0);
  DrawScreenString(_state, DS.distanceText.offset, INK_3, _segment, CHART_TEXT_LINE_2);
  GenerateSystemName(_state);
  DrawScreenString(_state, DS.selectedSystemName.offset, INK_2, _segment, CHART_TEXT_LINE_1);
  DrawScreenString(_state, DS.nameLinePadding.offset, INK_2, _segment, CHART_NAME_PADDING);
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

void FindSystemByName(GameState& _state, Hardware& _hardware, std::uint16_t _segment, std::uint16_t _countIfNone, bool _backward)
{
  LoadGalaxySeeds(_state);
  DrawScreenString(_state, DS.findPrompt.offset, INK_1, _segment, CHART_TEXT_LINE_1);
  DrawScreenString(_state, DS.blankChartLine.offset, INK_1, _segment, CHART_TEXT_LINE_2);
  for (std::uint16_t word = 0; word < SYSTEM_NAME_BYTES; word = static_cast<std::uint16_t>(word + 2))
  {
    _state.SetWord(Offset(DS.findInput.offset, word), 0);
  }
  (void)ReadTextLine(_state, _hardware, DS.findInput.offset, static_cast<std::uint8_t>(SYSTEM_NAME_BYTES), _segment, CHART_TEXT_LINE_2);
  if (_state.Byte(DS.findInput.offset) == 0)
  {
    // Nothing typed: on into ShowNearestSystemDistance, by a jump back to its entry, with ES as ReadTextLine leaves it.
    _hardware.LoopTurn(SHOW_NEAREST_SYSTEM_DISTANCE, {});
    const bool textLayout = _state.Get(DS.screenLayout) == TEXT_LAYOUT;
    ShowNearestSystemDistance(_state, _countIfNone, textLayout ? GameState::VIDEO_SEGMENT : _segment);
    return;
  }

  // The name echoed for the error message and upper-cased in place, a space after it; the count to compare its length,
  // counting the space when there is room for it. Each turn of the loop carries the two places and the length.
  std::uint16_t typed = DS.findInput.offset;
  std::uint16_t echo = DS.findInputEcho.offset;
  std::uint16_t length = 0;
  for (;;)
  {
    const std::uint8_t character = _state.Byte(typed);
    _state.SetByte(echo, character);
    if (character == 0)
    {
      break;
    }
    ++length;
    _state.SetByte(typed, static_cast<std::uint8_t>(_state.Byte(typed) & UPPER_CASE_MASK));
    typed = Offset(typed, 1);
    echo = Offset(echo, 1);
    _hardware.LoopTurn(FIND_UPPER_CASE, {typed, echo, length});
  }
  _state.SetByte(typed, SPACE);
  const auto count = static_cast<std::uint16_t>(length == SYSTEM_NAME_BYTES ? length : length + 1);

  // Each system's name in turn, compared with it by REPE CMPSB in the data segment. Each turn carries the system and the count.
  for (std::uint8_t system = 0;;)
  {
    GenerateSystemName(_state);
    if (CompareBytes(_state, DS.selectedSystemName.offset, _state.DataSegment(), DS.findInput.offset, count, _backward).equal)
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
  const std::uint8_t items = _state.Get(DS.chartItemCount);
  if (items == 0)
  {
    return false;
  }
  _state.Set(DS.drawColor, CHART_DISC_COLOR);
  bool discDrawn = false;
  std::uint16_t item = DS.chartItems.offset;
  // PUSH CX / PUSH DI round each item keep the count and the item: the LOOP's count is a local.
  for (std::uint8_t itemsLeft = items; itemsLeft != 0; --itemsLeft)
  {
    const std::uint16_t mark = _state.Word(Offset(item, CHART_ITEM_RADIUS));
    if (High(mark) != 0)
    {
      // A label, its text at its top-left corner: the x range's first byte in DL and the rows' first in DH.
      (void)DrawSmallViewString(_state, _state.Word(Offset(item, CHART_ITEM_TEXT)), INK_3,
                                MakeWord(_state.Byte(Offset(item, CHART_ITEM_X)), _state.Byte(Offset(item, CHART_ITEM_ROWS))));
    }
    else
    {
      // A disc: the radius in BX, the centre's x in DX and its row in CX.
      const std::uint16_t center = _state.Word(Offset(item, CHART_ITEM_TEXT));
      DrawDisc(_state, mark, Low(center), High(center), _backward);
      discDrawn = true;
    }
    item = Offset(item, CHART_ITEM_BYTES);
  }
  return discDrawn;
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

ScreenKey ShowSystemDataScreen(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _countIfNone)
{
  const PrintedText title =
    PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, DS.systemDataFrame.offset, _backward), DATA_TITLE);
  TerminateSelectedSystemName(_state);
  PrintTextModeString(_state, DS.selectedSystemName.offset, title.nextCell);
  PrintedText printed = PrintTextModeString(_state, DISTANCE_LABEL, DISTANCE_ROW);
  printed = PrintTextModeString(_state, FormatSelectedSystemDistance(_state), printed.nextCell);
  PrintTextModeString(_state, LIGHT_YEARS_LABEL, printed.nextCell);
  printed = PrintTextModeString(_state, ECONOMY_LABEL, ECONOMY_ROW);
  PrintNamed(_state, DS.economyNames.offset, static_cast<std::uint16_t>(_state.Get(DS.selectedEconomy) << 1), printed.nextCell);
  printed = PrintTextModeString(_state, GOVERNMENT_LABEL, GOVERNMENT_ROW);
  PrintNamed(_state, DS.governmentNames.offset, static_cast<std::uint16_t>(_state.Get(DS.selectedGovernment) << 1), printed.nextCell);
  printed = PrintTextModeString(_state, TECH_LEVEL_LABEL, TECH_LEVEL_ROW);

  // The tech level, one to sixteen, in the two characters at data7D48, a column left when it is one digit.
  auto level = static_cast<std::uint8_t>(_state.Get(DS.selectedTechLevel) + 1);
  auto cell = static_cast<std::uint16_t>(printed.nextCell - 2);
  _state.Set(DS.data7D48, SPACE);
  if (level >= TECH_LEVEL_TENS)
  {
    level = static_cast<std::uint8_t>(level - TECH_LEVEL_TENS);
    _state.Set(DS.data7D48, '1');
    cell = Offset(cell, 2);
  }
  _state.Set(DS.data7D49, static_cast<std::uint8_t>(level + '0'));
  PrintTextModeString(_state, DS.data7D48.offset, cell);

  // The population in tenths of a billion, its last two digits about a point.
  FormatDecimal5(_state, _state.Get(DS.selectedPopulationTenthsBillion), POPULATION_DIGITS);
  printed = PrintTextModeString(_state, POPULATION_LABEL, POPULATION_ROW);
  _state.SetByte(POPULATION_SHOWN, _state.Byte(Offset(POPULATION_SHOWN, 1)));
  _state.SetByte(Offset(POPULATION_SHOWN, 1), DECIMAL_POINT);
  PrintTextModeString(_state, POPULATION_SHOWN, printed.nextCell);

  printed = PrintTextModeString(_state, SPECIES_OPEN, SPECIES_ROW);
  const std::uint8_t size = _state.Get(DS.selectedSpeciesAdjective1);
  if (size == NO_SPECIES)
  {
    PrintTextModeString(_state, HUMAN_COLONIALS, printed.nextCell);
  }
  else
  {
    // SHL AL,1 on each byte, then XOR AH,AH.
    printed = PrintNamed(_state, DS.speciesSizeNames.offset, static_cast<std::uint8_t>(size << 1), printed.nextCell);
    printed = PrintNamed(_state, DS.speciesColorNames.offset, static_cast<std::uint8_t>(_state.Get(DS.selectedSpeciesAdjective2) << 1),
                         printed.nextCell);
    printed = PrintNamed(_state, DS.speciesTraitNames.offset, static_cast<std::uint8_t>(_state.Get(DS.selectedSpeciesAdjective3) << 1),
                         printed.nextCell);
    PrintNamed(_state, DS.speciesTypeNames.offset, static_cast<std::uint8_t>(_state.Get(DS.selectedSpeciesType) << 1), printed.nextCell);
  }

  // The productivity, its leading zero blanked a column left; the radius, its leading digit blanked.
  FormatDecimal5(_state, _state.Get(DS.selectedProductivityMillionCredits), DS.data7EA3.offset);
  printed = PrintTextModeString(_state, PRODUCTIVITY_LABEL, PRODUCTIVITY_ROW);
  cell = printed.nextCell;
  if (_state.Get(DS.data7EA3) == '0')
  {
    _state.Set(DS.data7EA3, SPACE);
    cell = static_cast<std::uint16_t>(cell - 2);
  }
  PrintTextModeString(_state, DS.data7EA3.offset, cell);
  FormatDecimal5(_state, _state.Get(DS.selectedRadiusKm), DS.data7EBB.offset);
  _state.Set(DS.data7EBB, SPACE);
  PrintTextModeString(_state, RADIUS_LABEL, RADIUS_ROW);
  ShowSystemDescription(_state, _backward);
  // ShowSystemDescription's last print leaves AL 0.
  return WaitForScreenExitKey(_state, _hardware, SCAN_F7, 0, _countIfNone);
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
// SelectSystemAtCursor's and ShowNearestSystemDistance's: all but the segment registers.
constexpr NativeContract SELECTS_SYSTEM{GENERAL, 0};
// FindSystemByName's: all but DS.
constexpr NativeContract FINDS_BY_NAME{static_cast<std::uint16_t>(GENERAL | REGISTER_ES), 0};
// ShowSystemDataScreen's: all but AX, the closing key in AH, and DS.
constexpr NativeContract SHOWS_SCREEN{static_cast<std::uint16_t>((GENERAL | REGISTER_ES) & ~REGISTER_AX), 0};
// ShowSystemDescription's: all but DS, which the original leaves alone and ShowSystemDataScreen goes on with.
constexpr NativeContract SHOWS_DESCRIPTION{static_cast<std::uint16_t>(REGISTER_ALL & ~REGISTER_DS), 0};
// The charts': all but AX, the closing key in AH, BP and ES, which the screen shown next reads, and DS (ChartOut).
constexpr NativeContract SHOWS_CHART{REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI, 0};
// DrawChartItems': all but ES, which DrawDisc leaves on the data segment, and DS.
constexpr NativeContract DRAWS_CHART_ITEMS{GENERAL, 0};

// What a chart leaves once _key closes it: AX the key, BP as SelectSystemAtCursor's search leaves it, and ES on the screen and
// the direction flag clear, as PresentChartFrame leaves them: the screen shown next draws through ES once the chart frame shows
// (DrawChartFrame), and the docked screens read BP as SelectSystemAtCursor's count when no system is on the chart.
void ChartOut(Guest& _guest, const ScreenKey& _key)
{
  Machine::Registers& regs = _guest.Regs();
  regs.ax = Join(_key.scanCode, _key.al);
  regs.bp = _key.countLeft.value_or(regs.bp);
  regs.es = GameState::VIDEO_SEGMENT;
  _guest.SetFlag(Machine::FLAG_DIRECTION, false);
}

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

void ShowGalacticChartEntry(Guest& _guest)
{
  const ScreenKey key = ShowGalacticChart(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION), _guest.Regs().es);
  ChartOut(_guest, key);
  _guest.Clobber(SHOWS_CHART);
}

void ShowShortRangeChartEntry(Guest& _guest)
{
  const ScreenKey key = ShowShortRangeChart(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION), _guest.Regs().es);
  ChartOut(_guest, key);
  _guest.Clobber(SHOWS_CHART);
}

void DrawChartItemsEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // DrawDisc's MOV AX,DS / MOV ES,AX, once a disc is drawn.
  if (DrawChartItems(_guest.State(), _guest.Flag(Machine::FLAG_DIRECTION)))
  {
    regs.es = regs.ds;
  }
  _guest.Clobber(DRAWS_CHART_ITEMS);
}

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

void SelectSystemAtCursorEntry(Guest& _guest)
{
  // BP, the count FindNearestSystem makes the index from when no system is on the chart.
  SelectSystemAtCursor(_guest.State(), _guest.Regs().bp);
  _guest.Clobber(SELECTS_SYSTEM);
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

void ShowNearestSystemDistanceEntry(Guest& _guest)
{
  const Machine::Registers& regs = _guest.Regs();
  ShowNearestSystemDistance(_guest.State(), regs.bp, regs.es);
  _guest.Clobber(SELECTS_SYSTEM);
}

void FindSystemByNameEntry(Guest& _guest)
{
  // ES, the segment the chart is drawn in; BP, the count FindNearestSystem makes the index from when no system is on the chart.
  const Machine::Registers& regs = _guest.Regs();
  FindSystemByName(_guest.State(), _guest.Devices(), regs.es, regs.bp, _guest.Flag(Machine::FLAG_DIRECTION));
  _guest.Clobber(FINDS_BY_NAME);
}

void ShowSystemDataScreenEntry(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  // BP, the count SelectSystemAtCursor makes the index from when no system is on the chart.
  const ScreenKey key = ShowSystemDataScreen(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION), regs.bp);
  regs.ax = Join(key.scanCode, key.al);
  _guest.Clobber(SHOWS_SCREEN);
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
  NativeEntry{0x0CAE, "ShowGalacticChart", &ShowGalacticChartEntry, SHOWS_CHART, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x0E52, "ShowShortRangeChart", &ShowShortRangeChartEntry, SHOWS_CHART, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x1076, "GetShortRangeOffset", &GetShortRangeOffsetEntry, CLOBBERS_AX_RETURNS_CARRY},
  NativeEntry{0x10AF, "IsSystemOnChart", &IsSystemOnChartEntry, RETURNS_CARRY},
  NativeEntry{0x10C0, "TwistSystemSeeds", &TwistSystemSeedsEntry, PRESERVES_ALL},
  NativeEntry{0x10D6, "LoadGalaxySeeds", &LoadGalaxySeedsEntry, PRESERVES_ALL},
  NativeEntry{0x10FE, "GetCursorGalaxyPosition", &GetCursorGalaxyPositionEntry, CLOBBERS_CX},
  NativeEntry{0x1146, "MoveCursorToSystem", &MoveCursorToSystemEntry, CLOBBERS_AX_BX},
  NativeEntry{0x1199, "SelectSystemAtCursor", &SelectSystemAtCursorEntry, SELECTS_SYSTEM},
  NativeEntry{0x1292, "FindNearestSystem", &FindNearestSystemEntry, FINDS_NEAREST},
  NativeEntry{0x12F9, "ComputeDistanceToSystem", &ComputeDistanceToSystemEntry, CLOBBERS_AX_BX_CX_DX},
  NativeEntry{0x1341, "ShowNearestSystemDistance", &ShowNearestSystemDistanceEntry, SELECTS_SYSTEM},
  NativeEntry{0x139C, "LoadSystemSeeds", &LoadSystemSeedsEntry, PRESERVES_ALL},
  NativeEntry{0x13B4, "AdvanceToNextSystem", &AdvanceToNextSystemEntry, PRESERVES_ALL},
  NativeEntry{0x13C1, "GenerateSystemName", &GenerateSystemNameEntry, CLOBBERS_BX_CX_SI},
  NativeEntry{0x140D, "FindSystemByName", &FindSystemByNameEntry, FINDS_BY_NAME, NativeReturn::Near, 0, NativeWait::Always},
  NativeEntry{0x14C5, "DrawChartItems", &DrawChartItemsEntry, DRAWS_CHART_ITEMS},
  NativeEntry{0x1505, "PlaceChartLabels", &PlaceChartLabelsEntry, PLACES_LABELS},
  NativeEntry{0x1552, "AddChartLabel", &AddChartLabelEntry, CLOBBERS_BX_DI},
  NativeEntry{0x157D, "NudgeChartLabel", &NudgeChartLabelEntry, RETURNS_CARRY},
  NativeEntry{0x15A9, "ChartItemOverlaps", &ChartItemOverlapsEntry, RETURNS_CARRY},
  NativeEntry{0x15BC, "ClearChartTextLines", &ClearChartTextLinesEntry, CLEARS_TEXT_LINES},
  NativeEntry{0x5CDE, "ShowSystemDataScreen", &ShowSystemDataScreenEntry, SHOWS_SCREEN, NativeReturn::Near, 0, NativeWait::Always},
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
