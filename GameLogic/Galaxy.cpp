#include "pch.h"

#include "Docked.h"
#include "Galaxy.h"

#include "Arithmetic.h"
#include "GalaxyTables.h"
#include "Input.h"
#include "Pacing.h"
#include "Video.h"

#include <initializer_list>
#include <optional>
#include <utility>

namespace Elite
{

namespace
{

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

// A description's control codes, in the order textControlCodes lists their handlers.
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
// The ninth galaxy, galaxyNumber 8, and its seeds: the six bytes after galaxySeeds' eight entries, gap29A6, of the original's
// image (GalaxyTests holds them to it), which the original reads as a ninth entry.
constexpr std::uint8_t NINTH_GALAXY = 8;
constexpr std::array<std::uint8_t, GALAXY_SEED_BYTES> NINTH_GALAXY_SEEDS = {0x34, 0x12, 0x78, 0x56, 0xBC, 0x9A};
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

// Where the data screen prints (text offsets). The fields it writes into are members (change 3c), and its labels and units are
// constants (GalaxyTables.h).
constexpr std::uint16_t DATA_TITLE = 0x0054;
constexpr std::uint16_t DISTANCE_ROW = 0x0144;
constexpr std::uint16_t ECONOMY_ROW = 0x01E4;
constexpr std::uint16_t GOVERNMENT_ROW = 0x0284;
constexpr std::uint16_t TECH_LEVEL_ROW = 0x0324;
constexpr std::size_t POPULATION_SHOWN = 2; // the units digit, where the point goes after it
constexpr std::uint16_t POPULATION_ROW = 0x03C4;
constexpr std::uint16_t SPECIES_ROW = 0x0414;
constexpr std::uint8_t NO_SPECIES = 0xFF;
constexpr std::uint16_t PRODUCTIVITY_ROW = 0x04B4;
constexpr std::uint16_t RADIUS_ROW = 0x0554;
constexpr std::uint8_t TECH_LEVEL_TENS = 10;

// Where InsertRandomName keeps selectedSystem.name while it borrows it: in invasionDebriefText's row.
constexpr std::size_t SAVED_SYSTEM_NAME = 0x9650 - 0x94B9;
constexpr std::uint16_t SYSTEM_NAME_BYTES = 8;
constexpr std::uint16_t ADJECTIVE_SUFFIX_BYTES = 5;
// A label in pendingChartLabels: its x range and its rows, two bytes each, the first then the last, then its name.
constexpr std::size_t CHART_LABEL_X = 0;
constexpr std::size_t CHART_LABEL_ROWS = 2;
constexpr std::size_t CHART_LABEL_NAME = 4;

constexpr std::uint16_t DESCRIPTION_BUFFER_WORDS = 0x80;
constexpr std::uint16_t DESCRIPTION_SCREEN_OFFSET = 0x5F4;
constexpr std::uint16_t DESCRIPTION_LINE_CHARACTERS = 0x24;
constexpr std::uint16_t TEXT_ROW_BYTES = 0x50;
constexpr std::uint8_t DESCRIPTION_PHRASE_CODE = 0x80;
constexpr std::uint8_t SEVEN_NAMES = 7; // the bits DescribeSystem gives the economy, the government and each of the species' bytes
constexpr std::uint8_t DESCRIPTION_PHRASE_DIVISOR = 0x34;
// The byte before descriptionBuffer in the original, which ExpandDescriptionText reads before the first character it writes: the NUL that
// ends descriptionTemplate.
constexpr char BEFORE_DESCRIPTION_BUFFER = DESCRIPTION_TEMPLATE.data()[DESCRIPTION_TEMPLATE.size()];
static_assert(BEFORE_DESCRIPTION_BUFFER == '\0');

[[nodiscard]] constexpr std::uint16_t MakeWord(std::uint8_t _low, std::uint8_t _high) noexcept
{
  return static_cast<std::uint16_t>(_low | (_high << 8));
}

// The difference of two bytes as a word, then negated when it is negative.
[[nodiscard]] std::uint16_t Magnitude(std::uint16_t _value) noexcept
{
  return (_value & 0x8000) != 0 ? static_cast<std::uint16_t>(0u - _value) : _value;
}

// The byte squared, as a word.
[[nodiscard]] std::uint16_t Square(std::uint8_t _value) noexcept
{
  return static_cast<std::uint16_t>(_value * _value);
}

// The galaxy-unit offset of a short-range chart coordinate from the chart's centre: doubled, then divided by 7, signed, into
// a byte (it cannot overflow), sign-extended, from _origin, clamped at 0.
[[nodiscard]] std::uint16_t ChartToGalaxy(std::uint16_t _fromCenter, std::uint8_t _origin) noexcept
{
  const auto doubled = static_cast<std::int16_t>(_fromCenter << 1);
  const auto quotient = static_cast<std::int8_t>(doubled / SHORT_RANGE_DIVISOR);
  const auto position = static_cast<std::uint16_t>(quotient + _origin);
  return (position & 0x8000) != 0 ? static_cast<std::uint16_t>(0) : position;
}

// 3.5 times a galaxy-unit offset (x*2 + x + x halved, signed), from the chart's centre.
[[nodiscard]] std::uint16_t GalaxyToChart(std::uint16_t _offset, std::uint16_t _center) noexcept
{
  const auto half = static_cast<std::uint16_t>(static_cast<std::int16_t>(_offset) >> 1);
  return static_cast<std::uint16_t>(_offset * 3 + half + _center);
}

[[nodiscard]] constexpr std::uint16_t Word(std::int16_t _value) noexcept
{
  return static_cast<std::uint16_t>(_value);
}

// The loop of byte moves some routines make, a byte at a time: each of _from's bytes to the same place of _to, in order.
void CopyBytes(std::span<const std::uint8_t> _from, std::span<std::uint8_t> _to)
{
  for (std::size_t done = 0; done < _from.size(); ++done)
  {
    _to[done] = _from[done];
  }
}

// The selected system's name: up to eight letters and its NUL, which TerminateSelectedSystemName writes after an eighth letter.
// The original reads the name as a text from its first byte, on past the eighth to a NUL; the ninth byte is 0 at the start and in
// every commander the game writes, and a NUL past it throws std::out_of_range (TextWithin).
template <typename State> [[nodiscard]] auto SystemNameField(State& _state)
{
  return std::span(_state.Data().commander.selectedSystem.name);
}

// _digits' characters, a field of the data screen the game writes, as a text (JoinTexts).
[[nodiscard]] std::string_view DigitsText(std::span<const std::uint8_t> _digits) noexcept
{
  return std::string_view(reinterpret_cast<const char*>(_digits.data()), _digits.size());
}

// Control code _code's handler, which the original reaches through textControlCodes, keeping the place in the text and the way
// back to ResumeTextExpansion; the handler returns there. The original keeps no more than those, so the handler's value routine
// runs on the output at _output. Codes 7-31 jump into descriptionPhraseLists, whose words the original would run as code; the
// routine runs nothing for them. No text in the game holds such a code (TableTests checks the phrases), and no
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

// What REPE CMPSB finds.
struct ByteComparison
{
  std::uint16_t compared; // the bytes it compared, which both places move on by and the count goes down by
  bool equal;             // every byte compared was equal: the zero flag it leaves
};

// REPE CMPSB, both places in the game's data: _source with _destination from their first bytes, for at most _count bytes, until two
// differ. A count of 0 compares nothing, and the flags keep what they held. Its one use compares a name with what was typed
// (FindTypedSystem).
[[nodiscard]] ByteComparison CompareBytes(std::span<const std::uint8_t> _source, std::span<const std::uint8_t> _destination,
                                          std::uint16_t _count)
{
  ByteComparison comparison{0, true};
  std::size_t source = 0;
  std::size_t destination = 0;
  while (comparison.compared < _count && comparison.equal)
  {
    const std::uint8_t sourceByte = ByteWithin(_source, source);
    const std::uint8_t destinationByte = ByteWithin(_destination, destination);
    comparison.equal = sourceByte == destinationByte;
    ++source;
    ++destination;
    ++comparison.compared;
  }
  return comparison;
}

// FindNearestSystem's search: the low byte of the count its loop had left at the nearest system on the current chart; or, when
// no system is on the chart, which never happens, 100h less the current system's index, where the original keeps what it came
// with (ADR-046). The index it selects is 100h less that count.
std::uint16_t SearchNearestSystem(GameState& _state)
{
  LoadGalaxySeeds(_state);
  const ChartPoint cursor = GetCursorGalaxyPosition(_state);
  // The nearest distance so far, and the low byte of the loop's count at the nearest.
  std::uint16_t nearest = 0xFFFF;
  std::uint16_t nearestCount = static_cast<std::uint16_t>(GALAXY_SYSTEMS - _state.Data().commander.currentSystem.index);
  for (std::uint16_t count = GALAXY_SYSTEMS; count != 0; --count)
  {
    // dx^2 + (dy/2)^2 to the cursor, each square a byte multiply; a sum that carries is passed over.
    const std::uint16_t across = Square(Low(Magnitude(static_cast<std::uint16_t>(SystemX(_state) - cursor.x))));
    const std::uint16_t down = Square(Low(Magnitude(static_cast<std::uint16_t>((SystemY(_state) >> 1) - cursor.row))));
    const std::uint32_t distance = std::uint32_t{across} + down;
    if (distance <= 0xFFFF && distance < nearest && IsSystemOnChart(_state))
    {
      nearest = static_cast<std::uint16_t>(distance);
      nearestCount = Low(count);
    }
    AdvanceToNextSystem(_state);
  }
  const auto index = static_cast<std::uint8_t>(GALAXY_SYSTEMS - nearestCount);
  _state.Data().commander.selectedSystem.index = index;
  LoadSystemSeeds(_state, index);
  MoveCursorToSystem(_state);
  return nearestCount;
}

// What SelectSystemAtCursor and NoteNearestSystem begin with: FindNearestSystem, ComputeDistanceToSystem, and the
// distance, read back from selectedSystem.distanceTenthsLy, into distanceDigits with up to three leading zeros blanked.
void SelectNearestSystem(GameState& _state)
{
  (void)SearchNearestSystem(_state);
  ComputeDistanceToSystem(_state);
  const std::span<std::uint8_t> digits(_state.Data().galaxy.distanceDigits);
  FormatDecimal5(_state.Data().commander.selectedSystem.distanceTenthsLy, digits);
  BlankLeadingZeros(digits, DISTANCE_ZEROS_BLANKED);
}

} // namespace

void EnterChart(GameState& _state, Host& _host, ChartKind _kind)
{
  CommanderState& commander = _state.Data().commander;
  _state.Data().drawing.sunFringeMask = 0;
  if (_kind == ChartKind::Galactic)
  {
    LoadSystemSeeds(_state, commander.currentSystem.index);
    commander.currentSystemX = std::uint8_t{SystemX(_state)};
    commander.currentSystemChartY = static_cast<std::uint8_t>(SystemY(_state) >> 1);
  }
  (void)DrawChartFrame(_state, _host);
  if (_kind == ChartKind::Galactic)
  {
    _state.Data().galaxy.galacticChartNumber = static_cast<std::uint8_t>(commander.galaxyNumber + '1');
    commander.chartCursorX = std::uint8_t{commander.galacticCursorX};
    commander.chartCursorY = std::uint8_t{commander.galacticCursorY};
    commander.chartIsShortRange = 0;
    return;
  }
  commander.chartCursorX = std::uint8_t{commander.shortRangeCursorX};
  commander.chartCursorY = std::uint8_t{commander.shortRangeCursorY};
  commander.chartIsShortRange = 1;
}

void LeaveChart(GameState& _state)
{
  if (_state.Data().jump.hyperspaceCountdown != 0)
  {
    return;
  }
  SelectSystemAtCursor(_state);
  CommanderState& commander = _state.Data().commander;
  const bool shortRange = commander.chartIsShortRange == 1;
  (shortRange ? commander.shortRangeCursorX : commander.galacticCursorX) = std::uint8_t{commander.chartCursorX};
  (shortRange ? commander.shortRangeCursorY : commander.galacticCursorY) = std::uint8_t{commander.chartCursorY};
}

void RecenterChartCursor(GameState& _state)
{
  CommanderState& commander = _state.Data().commander;
  if (commander.chartIsShortRange == 1)
  {
    commander.chartCursorX = SHORT_RANGE_CENTER.x;
    commander.chartCursorY = SHORT_RANGE_CENTER.row;
    return;
  }
  commander.chartCursorX = std::uint8_t{commander.currentSystemX};
  commander.chartCursorY = std::uint8_t{commander.currentSystemChartY};
}

void PlaceChartCursor(GameState& _state, ChartClick _point)
{
  _state.Data().commander.chartCursorX = _point.x;
  _state.Data().commander.chartCursorY = _point.row < CHART_ROWS ? _point.row : CURSOR_LOWEST_ROW;
}

SystemFacts SelectedSystemFacts(GameState& _state)
{
  TerminateSelectedSystemName(_state);
  const SystemRecord& system = _state.Data().commander.selectedSystem;
  SystemFacts facts;
  facts.name = std::string(TextWithin(SystemNameField(std::as_const(_state)), 0));
  facts.index = system.index;
  facts.distanceTenths = system.distanceTenthsLy;
  facts.economy = std::string(ECONOMY_NAMES[system.economy & SEVEN_NAMES]);
  facts.government = std::string(GOVERNMENT_NAMES[system.government & SEVEN_NAMES]);
  facts.techLevel = static_cast<std::uint8_t>(system.techLevel + 1);
  facts.populationTenthsBillion = system.populationTenthsBillion;
  // The species as Data on System prints it between its brackets: its four names, each with its space, or Human Colonials.
  std::string species =
    system.speciesAdjective1 == NO_SPECIES
      ? std::string(HUMAN_COLONIALS_TEXT)
      : JoinTexts({SPECIES_SIZE_NAMES[system.speciesAdjective1 & SEVEN_NAMES], SPECIES_COLOR_NAMES[system.speciesAdjective2 & SEVEN_NAMES],
                   SPECIES_TRAIT_NAMES[system.speciesAdjective3 & SEVEN_NAMES], SPECIES_TYPE_NAMES[system.speciesType & SEVEN_NAMES]});
  if (!species.empty() && species.back() == ')')
  {
    species.pop_back();
  }
  facts.species = species;
  facts.productivityMillionCredits = system.productivityMillionCredits;
  facts.radiusKm = system.radiusKm;
  facts.description = SystemDescription(_state);
  return facts;
}

std::vector<ChartStar> ChartStars(GameState& _state)
{
  std::vector<ChartStar> stars;
  LoadGalaxySeeds(_state);
  const bool shortRange = _state.Data().commander.chartIsShortRange == 1;
  // Each of the 256 systems: a byte counted up from 0 until it wraps.
  for (std::uint8_t systems = 0;;)
  {
    // The larger of the short-range chart's two discs, as its radius of 6 rather than 4.
    const bool large = (SystemY(_state) & 1) != 0;
    if (!shortRange)
    {
      stars.push_back(ChartStar{.x = SystemX(_state), .row = static_cast<std::uint8_t>(SystemY(_state) >> 1), .large = large, .name = {}});
      AdvanceToNextSystem(_state);
    }
    else if (const ShortRangeOffset offset = GetShortRangeOffset(_state); offset.onChart)
    {
      const std::uint8_t x = Low(GalaxyToChart(Word(offset.x.value_or(0)), SHORT_RANGE_CENTER_X));
      const std::uint8_t row = Low(GalaxyToChart(Word(offset.row.value_or(0)), SHORT_RANGE_CENTER_ROW));
      const std::uint8_t length = GenerateSystemName(_state);
      const std::string_view name = TextWithin(SystemNameField(std::as_const(_state)), 0);
      stars.push_back(ChartStar{.x = x, .row = row, .large = large, .name = std::string(name.substr(0, length))});
    }
    else
    {
      AdvanceToNextSystem(_state);
    }
    systems = static_cast<std::uint8_t>(systems + 1);
    if (systems == 0)
    {
      return stars;
    }
  }
}

ChartClick SystemChartPlace(GameState& _state, std::uint8_t _system)
{
  LoadSystemSeeds(_state, _system);
  MoveCursorToSystem(_state);
  return ChartClick{.x = _state.Data().commander.chartCursorX, .row = _state.Data().commander.chartCursorY};
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
  // Every system is on the galactic chart, as GetShortRangeOffset finds too.
  return GetShortRangeOffset(_state).onChart;
}

void TwistSystemSeeds(GameState& _state)
{
  const SystemSeeds seeds = ReadSystemSeeds(_state);
  // In the order the original writes them: two exchanges, then the sum.
  _state.Data().galaxy.systemSeed1 = seeds.seed2;
  _state.Data().galaxy.systemSeed0 = seeds.seed1;
  _state.Data().galaxy.systemSeed2 = static_cast<std::uint16_t>(seeds.seed0 + seeds.seed1 + seeds.seed2);
}

void LoadGalaxySeeds(GameState& _state)
{
  // The table offset is figured in a byte: galaxyNumber times six, from galaxySeeds. The eight galaxies' seeds are its entries,
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

void SelectSystemAtCursor(GameState& _state)
{
  SelectNearestSystem(_state);
  DescribeSystem(_state);
}

void DescribeSystem(GameState& _state)
{
  const std::uint8_t seed1 = Low(_state.Data().galaxy.systemSeed1);
  const std::uint8_t seed2 = Low(_state.Data().galaxy.systemSeed2);
  const std::uint8_t seed2High = SystemSeed2High(_state);
  const std::uint8_t x = SystemX(_state);
  const std::uint8_t y = SystemY(_state);

  const auto government = static_cast<std::uint8_t>((seed1 >> 3) & 7);
  _state.Data().commander.selectedSystem.government = government;
  // Governments 0 and 1 set bit 1 of the economy before it is inverted.
  const auto economy = static_cast<std::uint8_t>((((government >> 1) == 0 ? 2 : 0) | (y & 7)) ^ 7);
  _state.Data().commander.selectedSystem.economy = economy;
  const auto techLevel = static_cast<std::uint8_t>(((economy + 3) & x) + (seed2 & 1));
  _state.Data().commander.selectedSystem.techLevel = techLevel;
  const auto population =
    static_cast<std::uint8_t>((Low(static_cast<std::uint16_t>(techLevel * economy)) >> 1) + 0x14 + ((seed1 >> 3) & 7));
  _state.Data().commander.selectedSystem.populationTenthsBillion = population;

  if ((seed2 & 0x80) == 0)
  {
    _state.Data().commander.selectedSystem.speciesAdjective1 = 0xFF;
  }
  else
  {
    const auto mixed = static_cast<std::uint8_t>((x ^ y) & 7);
    _state.Data().commander.selectedSystem.speciesAdjective1 = static_cast<std::uint8_t>((seed2High >> 2) & 7);
    _state.Data().commander.selectedSystem.speciesAdjective2 = static_cast<std::uint8_t>(seed2High >> 5);
    _state.Data().commander.selectedSystem.speciesAdjective3 = mixed;
    _state.Data().commander.selectedSystem.speciesType = static_cast<std::uint8_t>(((seed2High & 3) + mixed) & 7);
  }

  // (government+8)^2, kept to a byte, times the population, times 4.
  const std::uint8_t squared = Low(Square(static_cast<std::uint8_t>(government + 8)));
  _state.Data().commander.selectedSystem.productivityMillionCredits = static_cast<std::uint16_t>((squared * population) << 2);

  const std::uint16_t rows = MakeWord(y, y);
  const auto rotated = static_cast<std::uint16_t>((rows << 2) | (rows >> 14));
  const std::uint16_t seed2Word = _state.Data().galaxy.systemSeed2;
  const auto swapped = static_cast<std::uint16_t>(((seed2Word >> 8) | (seed2Word << 8)) & 0x3FF);
  _state.Data().commander.selectedSystem.radiusKm = static_cast<std::uint16_t>(((rotated ^ swapped) & 0xFFF) + 0x10E1);

  const auto descriptionSeed0 = static_cast<std::uint16_t>(_state.Data().galaxy.systemSeed0 ^ _state.Data().galaxy.systemSeed1);
  _state.Data().commander.descriptionSeed0 = descriptionSeed0;
  _state.Data().commander.descriptionSeed1 = static_cast<std::uint16_t>(descriptionSeed0 ^ _state.Data().galaxy.systemSeed2);
  GenerateSystemName(_state);
}

std::uint8_t FindNearestSystem(GameState& _state)
{
  return static_cast<std::uint8_t>(GALAXY_SYSTEMS - SearchNearestSystem(_state));
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
  _state.Data().commander.selectedSystem.distanceTenthsLy = distanceTenthsLy;
}

void NoteNearestSystem(GameState& _state)
{
  SelectNearestSystem(_state);
  // The distance's four digits into distanceText, either side of its point.
  constexpr std::array<std::size_t, 4> PLACES = {DISTANCE_TEXT_HUNDREDS, DISTANCE_TEXT_TENS, DISTANCE_TEXT_UNITS, DISTANCE_TEXT_TENTHS};
  for (std::size_t digit = 0; digit < PLACES.size(); ++digit)
  {
    _state.Data().galaxy.distanceText[PLACES[digit]] = std::uint8_t{_state.Data().galaxy.distanceDigits[digit + 1]};
  }
  GenerateSystemName(_state);
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
  // Characters 7-8, then 5-6, blanked, two words of spaces.
  const std::span<std::uint8_t> name = SystemNameField(_state);
  for (std::size_t blanked = SYSTEM_NAME_BYTES; blanked > SYSTEM_NAME_BYTES / 2; --blanked)
  {
    name[blanked - 1] = SPACE;
  }
  // The fourth pair only when bit 6 of systemSeed0 was set.
  const bool fourPairs = (_state.Data().galaxy.systemSeed0 & SYSTEM_NAME_FOURTH_PAIR) != 0;
  std::size_t at = 0;
  std::uint8_t length = 0;
  for (std::uint16_t pair = 0; pair < SYSTEM_NAME_PAIRS; ++pair)
  {
    const std::uint8_t pick = SystemSeed2High(_state);
    TwistSystemSeeds(_state);
    const std::array<std::uint8_t, 2>& letters = SYSTEM_NAME_DIGRAMS[pick & 0x1F];
    const std::uint16_t digram = MakeWord(letters[0], letters[1]);
    if (pair + 1 == SYSTEM_NAME_PAIRS && !fourPairs)
    {
      continue;
    }
    // A word: a letter of the pair that is a space is written, and the next pair overwrites it.
    name[at] = Low(digram);
    name[at + 1] = High(digram);
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
  _state.Data().commander.selectedSystem.nameLength = length;
  return length;
}

FindResult FindTypedSystem(GameState& _state)
{
  LoadGalaxySeeds(_state);
  const std::span<std::uint8_t> typed(_state.Data().galaxy.findInput);
  if (typed[0] == 0)
  {
    NoteNearestSystem(_state);
    return FindResult::Nearest;
  }

  // The name echoed for the error message and upper-cased in place, a space after it; the count to compare its length,
  // counting the space when there is room for it. ReadTextLine leaves a NUL within findInput's nine bytes.
  const std::span<std::uint8_t> echo(_state.Data().galaxy.findInputEcho);
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
  }
  typed[length] = SPACE;
  const auto count = static_cast<std::uint16_t>(length == SYSTEM_NAME_BYTES ? length : length + 1);

  // Each system's name in turn, compared with it by REPE CMPSB in the data segment.
  for (std::uint8_t system = 0;;)
  {
    GenerateSystemName(_state);
    if (CompareBytes(SystemNameField(std::as_const(_state)), _state.Data().galaxy.findInput, count).equal)
    {
      LoadGalaxySeeds(_state);
      LoadSystemSeeds(_state, system);
      if (!GetShortRangeOffset(_state).onChart)
      {
        return FindResult::NotOnChart;
      }
      MoveCursorToSystem(_state);
      NoteNearestSystem(_state);
      return FindResult::Found;
    }
    ++system;
    if (system == 0)
    {
      break;
    }
  }
  return FindResult::NotOnChart;
}

[[nodiscard]] Task<ScreenKey> ShowSystemDataScreen(GameState& _state, Host& _host, ScreenPlace _place)
{
  const std::uint16_t titleEnd = PrintTextModeString(_state, DrawDockedFrame(_state, _host, SYSTEM_DATA_FRAME), DATA_TITLE);
  TerminateSelectedSystemName(_state);
  PrintTextModeString(_state, TextWithin(SystemNameField(_state), 0), titleEnd);
  std::uint16_t printed = PrintTextModeString(_state, DISTANCE_LABEL_TEXT, DISTANCE_ROW);
  printed = PrintTextModeString(_state, FormatSelectedSystemDistance(_state), printed);
  PrintTextModeString(_state, LIGHT_YEARS_TEXT, printed);
  printed = PrintTextModeString(_state, ECONOMY_LABEL_TEXT, ECONOMY_ROW);
  PrintTextModeString(_state, ECONOMY_NAMES[_state.Data().commander.selectedSystem.economy & SEVEN_NAMES], printed);
  printed = PrintTextModeString(_state, GOVERNMENT_LABEL_TEXT, GOVERNMENT_ROW);
  PrintTextModeString(_state, GOVERNMENT_NAMES[_state.Data().commander.selectedSystem.government & SEVEN_NAMES], printed);
  printed = PrintTextModeString(_state, TECH_LEVEL_LABEL_TEXT, TECH_LEVEL_ROW);

  // The tech level, one to sixteen, in the two characters at techLevelTensDigit, a column left when it is one digit.
  auto level = static_cast<std::uint8_t>(_state.Data().commander.selectedSystem.techLevel + 1);
  auto cell = static_cast<std::uint16_t>(printed - 2);
  _state.Data().galaxy.techLevelTensDigit = SPACE;
  if (level >= TECH_LEVEL_TENS)
  {
    level = static_cast<std::uint8_t>(level - TECH_LEVEL_TENS);
    _state.Data().galaxy.techLevelTensDigit = '1';
    cell = Offset(cell, 2);
  }
  _state.Data().galaxy.techLevelUnitsDigit = static_cast<std::uint8_t>(level + '0');
  PrintTextModeString(
    _state, JoinTexts({CharText(_state.Data().galaxy.techLevelTensDigit), CharText(_state.Data().galaxy.techLevelUnitsDigit)}), cell);

  // The population in tenths of a billion, its last two digits about a point.
  const std::span<std::uint8_t> population = BytesOf(_state.Data().galaxy.populationDigits);
  FormatDecimal5(_state.Data().commander.selectedSystem.populationTenthsBillion, population);
  printed = PrintTextModeString(_state, POPULATION_LABEL_TEXT, POPULATION_ROW);
  population[POPULATION_SHOWN] = std::uint8_t{population[POPULATION_SHOWN + 1]};
  population[POPULATION_SHOWN + 1] = DECIMAL_POINT;
  PrintTextModeString(_state, JoinTexts({DigitsText(population.subspan(POPULATION_SHOWN)), POPULATION_UNIT_TEXT}), printed);

  printed = PrintTextModeString(_state, SPECIES_OPEN_TEXT, SPECIES_ROW);
  const std::uint8_t size = _state.Data().commander.selectedSystem.speciesAdjective1;
  if (size == NO_SPECIES)
  {
    PrintTextModeString(_state, HUMAN_COLONIALS_TEXT, printed);
  }
  else
  {
    // Each of the species' four bytes indexes its table of names. DescribeSystem gives each three bits.
    const CommanderState& commander = _state.Data().commander;
    cell = PrintTextModeString(_state, SPECIES_SIZE_NAMES[size & SEVEN_NAMES], printed);
    cell = PrintTextModeString(_state, SPECIES_COLOR_NAMES[commander.selectedSystem.speciesAdjective2 & SEVEN_NAMES], cell);
    cell = PrintTextModeString(_state, SPECIES_TRAIT_NAMES[commander.selectedSystem.speciesAdjective3 & SEVEN_NAMES], cell);
    PrintTextModeString(_state, SPECIES_TYPE_NAMES[commander.selectedSystem.speciesType & SEVEN_NAMES], cell);
  }

  // The productivity, its leading zero blanked a column left; the radius, its leading digit blanked.
  GalaxyState& galaxy = _state.Data().galaxy;
  FormatDecimal5(_state.Data().commander.selectedSystem.productivityMillionCredits, galaxy.productivityDigits);
  cell = PrintTextModeString(_state, PRODUCTIVITY_LABEL_TEXT, PRODUCTIVITY_ROW);
  if (galaxy.productivityDigits[0] == '0')
  {
    galaxy.productivityDigits[0] = SPACE;
    cell = static_cast<std::uint16_t>(cell - 2);
  }
  PrintTextModeString(_state, JoinTexts({DigitsText(galaxy.productivityDigits), PRODUCTIVITY_UNIT_TEXT}), cell);
  FormatDecimal5(_state.Data().commander.selectedSystem.radiusKm, galaxy.radiusDigits);
  galaxy.radiusDigits[0] = SPACE;
  PrintTextModeString(_state, JoinTexts({RADIUS_LABEL, DigitsText(galaxy.radiusDigits), RADIUS_UNIT_TEXT}), RADIUS_ROW);
  ShowSystemDescription(_state);
  co_return co_await WaitForScreenExitKey(_state, _host, SCAN_F7, _place);
}

void TerminateSelectedSystemName(GameState& _state)
{
  // GenerateSystemName's length, at most eight, wherever the game writes it: the NUL goes in the name field.
  const std::uint8_t length = _state.Data().commander.selectedSystem.nameLength;
  ByteWithin(SystemNameField(_state), length) = 0;
}

std::string_view FormatSelectedSystemDistance(GameState& _state)
{
  // Backwards from the digits' end: the copy runs until it has copied a space, which SelectNearestSystem leaves before the digits: the
  // distance is below 10000 tenths of a light year (it is at most four times the root of 255^2 + 127^2), so its first digit is a
  // zero BlankLeadingZeros blanks, and the copy stops there at the latest.
  const std::span<const std::uint8_t> digits(_state.Data().galaxy.distanceDigits);
  const std::span<std::uint8_t> texts(_state.Data().galaxy.selectedDistanceText);
  std::size_t from = digits.size() - 1;
  std::size_t to = texts.size() - 1;
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
  return DigitsText(texts.subspan(to + 1));
}

std::string SystemDescription(GameState& _state)
{
  // Zeros over descriptionBuffer, a word at a time, from its start, forwards.
  const std::span<std::uint8_t> buffer(_state.Data().galaxy.descriptionBuffer);
  std::uint16_t word = 0;
  for (std::uint16_t words = DESCRIPTION_BUFFER_WORDS; words != 0; --words)
  {
    for (std::uint8_t& byte : BytesWithin(buffer, word, 2))
    {
      byte = 0;
    }
    word = Offset(word, 2);
  }
  const std::uint16_t expanded = ExpandDescriptionText(_state, DESCRIPTION_TEMPLATE, 0);
  ByteWithin(buffer, expanded) = 0;
  return std::string(TextWithin(buffer, 0));
}

void ShowSystemDescription(GameState& _state)
{
  (void)SystemDescription(_state);
  const std::span<std::uint8_t> buffer(_state.Data().galaxy.descriptionBuffer);

  // Word-wrapped at the last space within 36 characters, a text row at a time: line and end, offsets in the buffer.
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
  const std::span<std::uint8_t> buffer(_state.Data().galaxy.descriptionBuffer);
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
    // The byte before the output, which the original reads just behind it: before the buffer's first byte, the original's byte before it,
    // descriptionTemplate's NUL (DESCRIPTION_TEMPLATE's nine bytes end with it).
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
  // The space and the NUL after the name again, written as one word.
  const std::size_t space = CopySelectedNameLower(_state);
  const std::span<std::uint8_t> end = BytesWithin(std::span(_state.Data().galaxy.descriptionNameBuffer), space, 2);
  end[0] = SPACE;
  end[1] = 0;
  return ExpandDescriptionText(_state, TextWithin(_state.Data().galaxy.descriptionNameBuffer, 0), _output);
}

std::uint16_t InsertSystemAdjective(GameState& _state, std::uint16_t _output)
{
  // The name has a first letter: CopySelectedNameLower copies it whatever it is.
  std::size_t suffix = CopySelectedNameLower(_state);
  const std::span<std::uint8_t> name =
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
  const std::span<std::uint8_t> selected = SystemNameField(_state).first(SYSTEM_NAME_BYTES);
  const std::span<std::uint8_t> saved = std::span(_state.Data().galaxy.invasionDebriefText).subspan(SAVED_SYSTEM_NAME, SYSTEM_NAME_BYTES);
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
  const std::span<const std::uint8_t> selected = SystemNameField(std::as_const(_state));
  const std::size_t length = 1 + TextWithin(selected, 1).size();
  const std::span<const std::uint8_t> from = BytesWithin(selected, 0, length);
  const std::span<std::uint8_t> to = BytesWithin(std::span(_state.Data().galaxy.descriptionNameBuffer), 0, length + 2);
  to[0] = std::uint8_t{from[0]};
  for (std::size_t letter = 1; letter < length; ++letter)
  {
    to[letter] = static_cast<std::uint8_t>(from[letter] | LOWER_CASE_BIT);
  }
  // The space, then the NUL.
  to[length] = SPACE;
  to[length + 1] = 0;
  return length;
}

} // namespace Elite
