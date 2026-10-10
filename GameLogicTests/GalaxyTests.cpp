#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "Guest.h"

#include <array>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

constexpr std::uint16_t GET_SHORT_RANGE_OFFSET = 0x1076;
constexpr std::uint16_t IS_SYSTEM_ON_CHART = 0x10AF;
constexpr std::uint16_t TWIST_SYSTEM_SEEDS = 0x10C0;
constexpr std::uint16_t LOAD_GALAXY_SEEDS = 0x10D6;
constexpr std::uint16_t GET_CURSOR_GALAXY_POSITION = 0x10FE;
constexpr std::uint16_t MOVE_CURSOR_TO_SYSTEM = 0x1146;
constexpr std::uint16_t SELECT_SYSTEM_AT_CURSOR = 0x1199;
constexpr std::uint16_t FIND_NEAREST_SYSTEM = 0x1292;
constexpr std::uint16_t COMPUTE_DISTANCE_TO_SYSTEM = 0x12F9;
constexpr std::uint16_t LOAD_SYSTEM_SEEDS = 0x139C;
constexpr std::uint16_t ADVANCE_TO_NEXT_SYSTEM = 0x13B4;
constexpr std::uint16_t GENERATE_SYSTEM_NAME = 0x13C1;
constexpr std::uint16_t DRAW_CHART_ITEMS = 0x14C5;
constexpr std::uint16_t PLACE_CHART_LABELS = 0x1505;
constexpr std::uint16_t ADD_CHART_LABEL = 0x1552;
constexpr std::uint16_t NUDGE_CHART_LABEL = 0x157D;
constexpr std::uint16_t CHART_ITEM_OVERLAPS = 0x15A9;
constexpr std::uint16_t TERMINATE_SELECTED_SYSTEM_NAME = 0x60EB;
constexpr std::uint16_t FORMAT_SELECTED_SYSTEM_DISTANCE = 0x60F7;
constexpr std::uint16_t SHOW_SYSTEM_DESCRIPTION = 0x6FC0;
constexpr std::uint16_t EXPAND_DESCRIPTION_TEXT = 0x700F;
constexpr std::uint16_t NEXT_DESCRIPTION_RANDOM = 0x7115;
constexpr std::uint16_t COPY_SELECTED_NAME_LOWER = 0x7124;

// Scratch in the space view buffer, which nothing reads between frames of the title.
constexpr std::uint16_t CODED_TEXT = 0x1000;
constexpr std::uint16_t EXPANDED_TEXT = 0x1800;

constexpr std::uint8_t CHART_ITEM_BYTES = 8;

/// A position on a chart, or a system's coordinates.
struct Position
{
  std::uint8_t x;
  std::uint8_t y;
};

/// A chart item's or a pending label's box: x range, then row range.
struct ChartBox
{
  std::uint8_t left;
  std::uint8_t right;
  std::uint8_t top;
  std::uint8_t bottom;
};

/// A disc on the short-range chart.
struct Disc
{
  std::uint8_t x;
  std::uint8_t row;
  std::uint8_t radius;
};

/// A system's name as selectedSystemName holds it.
struct SystemName
{
  std::string_view text;
};

constexpr std::array<std::uint8_t, 3> CHART_MODES = {0, 1, 2};

[[nodiscard]] Elite::Guest GuestOf(ComparisonRig& _rig)
{
  return Elite::Guest(_rig.Host(), _rig.Program().loadSegment, Elite::DataSegment(_rig.Program()));
}

[[nodiscard]] std::wstring Hex(std::uint16_t _value)
{
  constexpr std::wstring_view DIGITS = L"0123456789ABCDEF";
  std::wstring text = L"CS:";
  for (int shift = 12; shift >= 0; shift -= 4)
    text += DIGITS[static_cast<std::size_t>((_value >> shift) & 0xF)];
  return text;
}

// Every offset in _offsets ran in the original while a call of _entry was being compared.
void AssertExecuted(ComparisonRig& _rig, std::uint16_t _entry, std::initializer_list<std::uint16_t> _offsets)
{
  const auto& hooks = _rig.Host().Native().Hooks();
  const auto found = hooks.find(Machine::Memory::Linear(_rig.Program().loadSegment, _entry));
  Assert::IsTrue(found != hooks.end(), L"the routine is ported");
  for (const std::uint16_t offset : _offsets)
  {
    const std::wstring message = Hex(offset) + L" ran in a comparison";
    Assert::IsTrue(found->second.executed.Contains(offset), message.c_str());
  }
}

// A fixed sequence of words, for inputs the replays do not give.
class Words
{
public:
  [[nodiscard]] std::uint16_t Next() noexcept
  {
    m_state = m_state * 1103515245u + 12345u;
    return static_cast<std::uint16_t>(m_state >> 16);
  }

private:
  std::uint32_t m_state = 1;
};

void SetText(Elite::Guest& _guest, std::uint16_t _offset, std::string_view _text)
{
  for (const char character : _text)
  {
    _guest.SetByte(_offset, static_cast<std::uint8_t>(character));
    ++_offset;
  }
}

// selectedSystemName and its length, as GenerateSystemName leaves them.
void SetSelectedName(Elite::Guest& _guest, const SystemName& _name)
{
  SetText(_guest, DS.selectedSystemName.offset, "        ");
  SetText(_guest, DS.selectedSystemName.offset, _name.text);
  _guest.Set(DS.selectedSystemNameLength, static_cast<std::uint8_t>(_name.text.size()));
}

// chartItems with _stars as discs, and pendingChartLabels with one label for each, named SYS1, SYS2...
void LayOutChart(Elite::Guest& _guest, std::span<const ChartBox> _stars, std::span<const ChartBox> _labels)
{
  auto item = DS.chartItems.offset;
  for (const ChartBox& star : _stars)
  {
    _guest.SetByte(item, star.left);
    _guest.SetByte(static_cast<std::uint16_t>(item + 1), star.right);
    _guest.SetByte(static_cast<std::uint16_t>(item + 2), star.top);
    _guest.SetByte(static_cast<std::uint16_t>(item + 3), star.bottom);
    _guest.SetByte(static_cast<std::uint16_t>(item + 4), static_cast<std::uint8_t>((star.left + star.right) / 2));
    _guest.SetByte(static_cast<std::uint16_t>(item + 5), static_cast<std::uint8_t>((star.top + star.bottom) / 2));
    _guest.SetByte(static_cast<std::uint16_t>(item + 6), 2);
    _guest.SetByte(static_cast<std::uint16_t>(item + 7), 0);
    item = static_cast<std::uint16_t>(item + CHART_ITEM_BYTES);
  }
  _guest.Set(DS.chartItemEnd, item);
  _guest.Set(DS.chartItemCount, static_cast<std::uint8_t>(_stars.size()));

  auto label = DS.pendingChartLabels.offset;
  char digit = '1';
  for (const ChartBox& box : _labels)
  {
    _guest.SetByte(label, box.left);
    _guest.SetByte(static_cast<std::uint16_t>(label + 1), box.right);
    _guest.SetByte(static_cast<std::uint16_t>(label + 2), box.top);
    _guest.SetByte(static_cast<std::uint16_t>(label + 3), box.bottom);
    SetText(_guest, static_cast<std::uint16_t>(label + 4), "SYS");
    _guest.SetByte(static_cast<std::uint16_t>(label + 7), static_cast<std::uint8_t>(digit));
    _guest.SetByte(static_cast<std::uint16_t>(label + 8), 0);
    label = static_cast<std::uint16_t>(label + 9);
    ++digit;
  }
}

} // namespace

// Constructed inputs for the ported galaxy routines (plan §6.3): the charts' edges, other galaxies and
// systems, and the description generator's every control code, which the replays reach only in part.
TEST_CLASS(GalaxyTests)
{
public:
  // On the galactic chart every system is on it at once (CS:107D); on the short-range one, the window's
  // edges either side of the current system.
  TEST_METHOD(ShortRangeOffsetAgreesOnBothChartsAndAtTheWindowsEdges)
  {
    ComparisonRig rig("GetShortRangeOffset");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<Position, 3> CURRENT = {{{0, 0}, {0x80, 0x40}, {0xFF, 0xFF}}};
    constexpr std::array<Position, 8> SYSTEMS = {
      {{0, 0}, {0x12, 0x22}, {0x13, 0x23}, {0x14, 0x24}, {0x7F, 0x7F}, {0x93, 0xA3}, {0x94, 0xA4}, {0xFF, 0xFF}}};
    std::uint64_t calls = 0;
    for (const std::uint8_t chart : CHART_MODES)
    {
      for (const Position& current : CURRENT)
      {
        for (const Position& system : SYSTEMS)
        {
          guest.Set(DS.chartIsShortRange, chart);
          guest.Set(DS.currentSystemX, current.x);
          guest.Set(DS.currentSystemChartY, current.y);
          guest.Set(DS.systemX, system.x);
          guest.Set(DS.systemY, system.y);
          rig.Call(GET_SHORT_RANGE_OFFSET, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444});
          rig.Call(IS_SYSTEM_ON_CHART, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444});
          ++calls;
        }
      }
    }
    rig.AssertAllAgreed(GET_SHORT_RANGE_OFFSET, calls);
    rig.AssertAllAgreed(IS_SYSTEM_ON_CHART, calls);
    AssertExecuted(rig, GET_SHORT_RANGE_OFFSET, {0x107D});
  }

  // A cursor left of or above the current system by more than it can reach clamps at 0 (CS:1127,
  // CS:113D); one right or below wraps at a byte.
  TEST_METHOD(CursorPositionAgreesWhereItClampsAndWraps)
  {
    ComparisonRig rig("GetCursorGalaxyPosition");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<Position, 4> CURSORS = {{{0, 0}, {0x50, 0x40}, {0xFF, 0xFF}, {0x10, 0x70}}};
    constexpr std::array<Position, 3> CURRENT = {{{0, 0}, {0x80, 0x40}, {0xFF, 0xFF}}};
    constexpr std::array<Position, 4> SYSTEMS = {{{0, 0}, {0x81, 0x7E}, {0xFF, 0xFF}, {0x7F, 0x01}}};
    std::uint64_t calls = 0;
    std::uint64_t moves = 0;
    for (const std::uint8_t chart : CHART_MODES)
    {
      for (const Position& current : CURRENT)
      {
        guest.Set(DS.chartIsShortRange, chart);
        guest.Set(DS.currentSystemX, current.x);
        guest.Set(DS.currentSystemChartY, current.y);
        for (const Position& cursor : CURSORS)
        {
          guest.Set(DS.chartCursorX, cursor.x);
          guest.Set(DS.chartCursorY, cursor.y);
          rig.Call(GET_CURSOR_GALAXY_POSITION, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444});
          ++calls;
        }
        for (const Position& system : SYSTEMS)
        {
          guest.Set(DS.systemX, system.x);
          guest.Set(DS.systemY, system.y);
          rig.Call(MOVE_CURSOR_TO_SYSTEM, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444});
          ++moves;
        }
      }
    }
    rig.AssertAllAgreed(GET_CURSOR_GALAXY_POSITION, calls);
    rig.AssertAllAgreed(MOVE_CURSOR_TO_SYSTEM, moves);
    AssertExecuted(rig, GET_CURSOR_GALAXY_POSITION, {0x1127, 0x113D});
  }

  // Every galaxy, both charts, the cursor in the corners and the middle: the nearest system, its
  // distance, data and name. In galaxy number 2 the short-range window around (0x58, 0) holds no system,
  // so FindNearestSystem never sets BP and the index comes from what BP held.
  TEST_METHOD(SelectingSystemsAgreesAcrossGalaxiesAndCharts)
  {
    ComparisonRig rig("SelectSystemAtCursor");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<Position, 5> CURSORS = {{{0, 0}, {0x50, 0x40}, {0xFF, 0x7F}, {0x10, 0x70}, {0xC0, 0x08}}};
    constexpr std::array<Position, 3> CURRENT = {{{0, 0}, {0x14, 0x30}, {0xF0, 0x70}}};
    std::uint64_t calls = 0;
    for (std::uint8_t galaxy = 0; galaxy < 8; ++galaxy)
    {
      guest.Set(DS.galaxyNumber, galaxy);
      for (std::uint8_t chart = 0; chart < 2; ++chart)
      {
        guest.Set(DS.chartIsShortRange, chart);
        for (const Position& current : CURRENT)
        {
          guest.Set(DS.currentSystemX, current.x);
          guest.Set(DS.currentSystemChartY, current.y);
          for (const Position& cursor : CURSORS)
          {
            guest.Set(DS.chartCursorX, cursor.x);
            guest.Set(DS.chartCursorY, cursor.y);
            rig.Call(SELECT_SYSTEM_AT_CURSOR,
                     {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666, .bp = 0x77});
            ++calls;
          }
        }
      }
    }
    guest.Set(DS.galaxyNumber, 2);
    guest.Set(DS.chartIsShortRange, 1);
    guest.Set(DS.currentSystemX, 0x58);
    guest.Set(DS.currentSystemChartY, 0);
    rig.Call(SELECT_SYSTEM_AT_CURSOR, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666, .bp = 0x77});
    ++calls;
    Assert::AreEqual(std::uint8_t{0x89}, guest.Get(DS.selectedSystemIndex), L"no system found: the index is 100h less BP");
    rig.AssertAllAgreed(SELECT_SYSTEM_AT_CURSOR, calls);
    AssertExecuted(rig, SELECT_SYSTEM_AT_CURSOR, {0x1127, 0x113D});
  }

  // The seed steps and the system loaders, from many seeds; LoadSystemSeeds ignores CH, and a galaxy
  // number past 7 reads past galaxySeeds, the offset wrapping at a byte.
  TEST_METHOD(SeedStepsAgreeFromManySeeds)
  {
    ComparisonRig rig("TwistSystemSeeds");
    Elite::Guest guest = GuestOf(rig);
    Words words;
    constexpr std::uint64_t SEEDS = 64;
    for (std::uint64_t seed = 0; seed < SEEDS; ++seed)
    {
      guest.Set(DS.systemSeed0, words.Next());
      guest.Set(DS.systemSeed1, words.Next());
      guest.Set(DS.systemSeed2, words.Next());
      rig.Call(TWIST_SYSTEM_SEEDS, {.ax = words.Next()});
      rig.Call(ADVANCE_TO_NEXT_SYSTEM, {.ax = words.Next()});
      rig.Call(GENERATE_SYSTEM_NAME, {.ax = words.Next(), .dx = words.Next(), .si = words.Next()});
    }
    constexpr std::array<std::uint8_t, 4> GALAXIES = {0, 7, 0x2B, 0xFF};
    constexpr std::array<std::uint16_t, 5> SYSTEMS = {0, 1, 7, 0xFF, 0xAB80};
    std::uint64_t loads = 0;
    for (const std::uint8_t galaxy : GALAXIES)
    {
      guest.Set(DS.galaxyNumber, galaxy);
      rig.Call(LOAD_GALAXY_SEEDS, {.ax = 0x1234, .bx = 0x5678});
      for (const std::uint16_t system : SYSTEMS)
        rig.Call(LOAD_SYSTEM_SEEDS, {.ax = 0x1234, .bx = 0x5678, .cx = system});
      ++loads;
    }
    rig.AssertAllAgreed(TWIST_SYSTEM_SEEDS, SEEDS);
    rig.AssertAllAgreed(ADVANCE_TO_NEXT_SYSTEM, SEEDS);
    rig.AssertAllAgreed(GENERATE_SYSTEM_NAME, SEEDS);
    rig.AssertAllAgreed(LOAD_GALAXY_SEEDS, loads);
    rig.AssertAllAgreed(LOAD_SYSTEM_SEEDS, loads * SYSTEMS.size());
  }

  // The routines SelectSystemAtCursor runs, called directly: the nearest system on each chart, and the
  // distance to systems in every direction.
  TEST_METHOD(NearestSystemAndDistanceAgreeDirectly)
  {
    ComparisonRig rig("FindNearestSystem");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<Position, 3> CURSORS = {{{0, 0}, {0x50, 0x40}, {0xFF, 0x7F}}};
    std::uint64_t calls = 0;
    for (std::uint8_t chart = 0; chart < 2; ++chart)
    {
      guest.Set(DS.chartIsShortRange, chart);
      for (const Position& cursor : CURSORS)
      {
        guest.Set(DS.chartCursorX, cursor.x);
        guest.Set(DS.chartCursorY, cursor.y);
        rig.Call(FIND_NEAREST_SYSTEM, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666, .bp = 0x77});
        ++calls;
      }
    }
    constexpr std::array<Position, 6> SYSTEMS = {{{0, 0}, {0xFF, 0xFF}, {0x80, 0x80}, {0x00, 0xFF}, {0xFF, 0x00}, {0x47, 0x9A}}};
    for (const Position& current : SYSTEMS)
    {
      guest.Set(DS.currentSystemX, current.x);
      guest.Set(DS.currentSystemChartY, static_cast<std::uint8_t>(current.y / 2));
      for (const Position& system : SYSTEMS)
      {
        guest.Set(DS.systemX, system.x);
        guest.Set(DS.systemY, system.y);
        rig.Call(COMPUTE_DISTANCE_TO_SYSTEM, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444});
      }
    }
    rig.AssertAllAgreed(FIND_NEAREST_SYSTEM, calls);
    rig.AssertAllAgreed(COMPUTE_DISTANCE_TO_SYSTEM, SYSTEMS.size() * SYSTEMS.size());
  }

  // Labels on top of one another, against the top and the bottom of the chart, and one no position
  // clears, which is placed where its 34 tries leave it.
  TEST_METHOD(LabelPlacementAgreesWhereLabelsCollide)
  {
    ComparisonRig rig("PlaceChartLabels");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<ChartBox, 6> STARS = {
      {{10, 14, 60, 64}, {40, 44, 0, 4}, {80, 84, 0x76, 0x7A}, {120, 124, 30, 34}, {200, 204, 90, 94}, {150, 154, 64, 68}}};
    constexpr std::array<ChartBox, 6> CROWDED = {
      {{8, 40, 58, 64}, {8, 40, 58, 64}, {8, 40, 58, 64}, {38, 70, 0, 6}, {78, 110, 0x75, 0x7B}, {8, 40, 58, 64}}};
    constexpr std::array<ChartBox, 1> EVERYWHERE = {{{0, 0xFF, 0, 0xFF}}};
    constexpr std::array<ChartBox, 1> ANYWHERE = {{{100, 130, 50, 56}}};
    LayOutChart(guest, STARS, CROWDED);
    rig.Call(PLACE_CHART_LABELS, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666, .bp = 0x7777});
    rig.Call(DRAW_CHART_ITEMS, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666, .bp = 0x7777});
    LayOutChart(guest, EVERYWHERE, ANYWHERE);
    rig.Call(PLACE_CHART_LABELS, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666, .bp = 0x7777});
    rig.AssertAllAgreed(PLACE_CHART_LABELS, 2);
    rig.AssertAllAgreed(DRAW_CHART_ITEMS, 1);

    // The pieces, called directly.
    LayOutChart(guest, STARS, CROWDED);
    std::uint64_t calls = 0;
    for (std::size_t item = 0; item < STARS.size(); ++item)
    {
      for (const ChartBox& box : CROWDED)
      {
        const auto di = static_cast<std::uint16_t>(DS.chartItems.offset + item * CHART_ITEM_BYTES);
        const auto bx = static_cast<std::uint16_t>(box.top | (box.bottom << 8));
        const auto dx = static_cast<std::uint16_t>(box.left | (box.right << 8));
        rig.Call(CHART_ITEM_OVERLAPS, {.bx = bx, .dx = dx, .di = di});
        ++calls;
      }
    }
    rig.AssertAllAgreed(CHART_ITEM_OVERLAPS, calls);
    constexpr std::array<std::uint8_t, 5> COUNTS = {0, 1, 0x20, 0x21, 0x22};
    constexpr std::array<std::uint16_t, 4> ROWS = {0x0000, 0x7B75, 0x4038, 0xFFF0};
    for (const std::uint8_t count : COUNTS)
    {
      for (const std::uint16_t rows : ROWS)
      {
        guest.Set(DS.labelNudgeCount, count);
        rig.Call(NUDGE_CHART_LABEL, {.bx = rows});
      }
    }
    rig.AssertAllAgreed(NUDGE_CHART_LABEL, COUNTS.size() * ROWS.size());
    guest.Set(DS.chartLabelCursor, DS.pendingChartLabels.offset);
    rig.Call(ADD_CHART_LABEL, {.bx = 0x4038, .dx = 0x6040});
    rig.Call(ADD_CHART_LABEL, {.bx = 0x0800, .dx = 0x2010});
    rig.AssertAllAgreed(ADD_CHART_LABEL, 2);
  }

  // Discs of every size at the chart's edges and beyond them, some with the sun's ragged fringe: what
  // DrawDisc does with each, under DrawChartItems.
  TEST_METHOD(ChartDiscsAgreeAtTheEdges)
  {
    ComparisonRig rig("DrawChartItems");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<std::uint8_t, 12> RADII = {0, 1, 2, 3, 4, 5, 6, 9, 20, 60, 120, 255};
    constexpr std::array<std::uint8_t, 6> COLUMNS = {0, 2, 128, 250, 253, 255};
    constexpr std::array<std::uint8_t, 6> ROWS = {0, 1, 64, 125, 127, 200};
    constexpr std::array<std::uint8_t, 2> FRINGES = {0, 7};
    std::uint64_t calls = 0;
    for (const std::uint8_t fringe : FRINGES)
    {
      guest.Set(DS.sunFringeMask, fringe);
      for (const std::uint8_t radius : RADII)
      {
        auto item = DS.chartItems.offset;
        std::uint8_t count = 0;
        for (const std::uint8_t column : COLUMNS)
        {
          for (const std::uint8_t row : ROWS)
          {
            guest.SetWord(static_cast<std::uint16_t>(item + 4), static_cast<std::uint16_t>(column | (row << 8)));
            guest.SetWord(static_cast<std::uint16_t>(item + 6), radius);
            item = static_cast<std::uint16_t>(item + CHART_ITEM_BYTES);
            ++count;
            if (count == 32)
              break;
          }
          if (count == 32)
            break;
        }
        guest.Set(DS.chartItemCount, count);
        rig.Call(DRAW_CHART_ITEMS, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666, .bp = 0x7777});
        ++calls;
      }
    }
    rig.AssertAllAgreed(DRAW_CHART_ITEMS, calls);
    // DrawDisc's radius-4-5 profile, the fringe, the clipped and the small-disc paths, and FillSpan's.
    AssertExecuted(rig, DRAW_CHART_ITEMS,
                   {0x1872, 0x1875, 0x1881, 0x1895, 0x1899, 0x189D, 0x18A1, 0x18A5, 0x18A7, 0x18A9, 0x18AD, 0x18B7, 0x18C5, 0x18C7,
                    0x190C, 0x1910, 0x1914, 0x1918, 0x191C, 0x191E, 0x1920, 0x1924, 0x192E, 0x193C, 0x193E, 0x194F, 0x196A, 0x196D,
                    0x196F, 0x1971, 0x1974, 0x197E, 0x1985, 0x1988, 0x198A, 0x198C, 0x198E, 0x1990, 0x1993, 0x1994, 0x1996, 0x1998,
                    0x19A5, 0x19A7, 0x19A9, 0x19E9, 0x19EB, 0x19ED, 0x19EF, 0x19F1, 0x19F5, 0x19F7, 0x19F9, 0x19FB, 0x19FD, 0x19FF,
                    0x1A03, 0x1A05, 0x1A54, 0x1A6D, 0x1A6F, 0x1A71, 0x1A73, 0x1A75, 0x1A77, 0x1A7B, 0x1A7D, 0x1A81, 0x1A83, 0x1A85,
                    0x1A89, 0x1A8B, 0x1A8F, 0x1A91, 0x1A93, 0x1A95, 0x1A97, 0x1A99, 0x1A9B, 0x1A9C, 0x1A9D, 0x1A9E, 0x1A9F});
  }

  // Every control code, the phrases, a double space, and capitalizing, which reaches 0x60-0x78 after a
  // space (CS:7064-7072) and so leaves y and z alone.
  TEST_METHOD(DescriptionTextAgreesForEveryControlCode)
  {
    ComparisonRig rig("ExpandDescriptionText");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<std::string_view, 6> TEXTS = {"\x05the yak zoo  ate `wax`  {food}\x06 then  stop",
                                                       "\x01 is \x02 and \x03 met \x01\x04\x04x",
                                                       "\x05\x80 \x81 \x82\x06 \x90 \x91 \x05\x92\x93 \xA6\x06",
                                                       " \x05 a b c x y z ` \x7F\x06 ",
                                                       "\x02\x02\x03\x03",
                                                       "plain text, nothing else."};
    constexpr std::array<SystemName, 5> NAMES = {{{"LAVE"}, {"DISO"}, {"RIEDQUAT"}, {"ZAONCE"}, {"USLERI"}}};
    Words words;
    std::uint64_t calls = 0;
    for (const SystemName& name : NAMES)
    {
      for (const std::string_view text : TEXTS)
      {
        for (const std::uint8_t before : {std::uint8_t{' '}, std::uint8_t{'q'}})
        {
          SetSelectedName(guest, name);
          SetText(guest, CODED_TEXT, text);
          guest.SetByte(static_cast<std::uint16_t>(CODED_TEXT + text.size()), 0);
          guest.SetByte(EXPANDED_TEXT - 1, before);
          guest.Set(DS.descriptionCapitalize, 0);
          guest.Set(DS.descriptionSeed0, words.Next());
          guest.Set(DS.descriptionSeed1, words.Next());
          rig.Call(EXPAND_DESCRIPTION_TEXT,
                   {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = CODED_TEXT, .di = EXPANDED_TEXT, .bp = 0x7777});
          ++calls;
        }
      }
      rig.Call(COPY_SELECTED_NAME_LOWER, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666});
      rig.Call(TERMINATE_SELECTED_SYSTEM_NAME, {.ax = 0x1111, .bx = 0x2222, .si = 0x5555});
      rig.Call(NEXT_DESCRIPTION_RANDOM, {.ax = 0x1111, .bx = 0x2222});
    }
    rig.AssertAllAgreed(EXPAND_DESCRIPTION_TEXT, calls);
    rig.AssertAllAgreed(COPY_SELECTED_NAME_LOWER, NAMES.size());
    rig.AssertAllAgreed(TERMINATE_SELECTED_SYSTEM_NAME, NAMES.size());
    rig.AssertAllAgreed(NEXT_DESCRIPTION_RANDOM, NAMES.size());
    AssertExecuted(rig, EXPAND_DESCRIPTION_TEXT, {0x7064, 0x7068, 0x706A, 0x706C, 0x706E, 0x7070, 0x7072});
  }

  // Whole descriptions, from many description seeds, and the distance text the data screen shows.
  TEST_METHOD(SystemDescriptionsAgreeFromManySeeds)
  {
    ComparisonRig rig("ShowSystemDescription");
    Elite::Guest guest = GuestOf(rig);
    constexpr std::array<SystemName, 4> NAMES = {{{"LAVE"}, {"ISINOR"}, {"TIBEDIED"}, {"ZA"}}};
    Words words;
    constexpr std::uint64_t DESCRIPTIONS = 96;
    for (std::uint64_t description = 0; description < DESCRIPTIONS; ++description)
    {
      SetSelectedName(guest, NAMES[description % NAMES.size()]);
      guest.Set(DS.descriptionSeed0, words.Next());
      guest.Set(DS.descriptionSeed1, words.Next());
      rig.Call(SHOW_SYSTEM_DESCRIPTION, {.ax = 0x1111, .bx = 0x2222, .cx = 0x3333, .dx = 0x4444, .si = 0x5555, .di = 0x6666, .bp = 0x7777});
    }
    rig.AssertAllAgreed(SHOW_SYSTEM_DESCRIPTION, DESCRIPTIONS);
    AssertExecuted(rig, SHOW_SYSTEM_DESCRIPTION, {0x7064, 0x7068, 0x706A, 0x706C, 0x706E, 0x7070, 0x7072});

    constexpr std::array<std::string_view, 4> DISTANCES = {"    0", "  123", " 4567", "65535"};
    for (const std::string_view distance : DISTANCES)
    {
      SetText(guest, DS.distanceDigits.offset, distance);
      guest.SetByte(static_cast<std::uint16_t>(DS.distanceDigits.offset - 1), ' ');
      rig.Call(FORMAT_SELECTED_SYSTEM_DISTANCE, {.ax = 0x1111, .bx = 0x2222, .si = 0x5555});
    }
    rig.AssertAllAgreed(FORMAT_SELECTED_SYSTEM_DISTANCE, DISTANCES.size());
  }
};

} // namespace GameLogicTests
