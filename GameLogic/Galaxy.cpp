#include "pch.h"

#include "Galaxy.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

// The original routines these call, which other subsystems port.
constexpr std::uint16_t DRAW_DISC = 0x1826;
constexpr std::uint16_t DRAW_SCREEN_STRING = 0x32D8;
constexpr std::uint16_t FORMAT_DECIMAL_5 = 0x3407;
constexpr std::uint16_t BLANK_LEADING_ZEROS = 0x3432;
constexpr std::uint16_t DRAW_SMALL_VIEW_STRING = 0x3527;
constexpr std::uint16_t PRINT_TEXT_MODE_STRING = 0x60D2;

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

[[nodiscard]] constexpr std::uint8_t Low(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word);
}

[[nodiscard]] constexpr std::uint8_t High(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word >> 8);
}

[[nodiscard]] constexpr std::uint16_t MakeWord(std::uint8_t _low, std::uint8_t _high) noexcept
{
  return static_cast<std::uint16_t>(_low | (_high << 8));
}

void SetLow(std::uint16_t& _word, std::uint8_t _value) noexcept
{
  _word = MakeWord(_value, High(_word));
}

void SetHigh(std::uint16_t& _word, std::uint8_t _value) noexcept
{
  _word = MakeWord(Low(_word), _value);
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
    --regs.di;
    break;
  case START_CAPITALIZING:
    _guest.Set(DS.descriptionCapitalize, 1);
    break;
  case STOP_CAPITALIZING:
    _guest.Set(DS.descriptionCapitalize, 0);
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

} // namespace

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
using Machine::REGISTER_ALL;
using Machine::REGISTER_AX;
using Machine::REGISTER_BP;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DI;
using Machine::REGISTER_DX;
using Machine::REGISTER_SI;

constexpr auto GENERAL =
  static_cast<std::uint16_t>(REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_SI | REGISTER_DI | REGISTER_BP);

// GenerateSystemName clobbers DL but returns DH: DX is compared whole, and the port leaves DL as the
// original does.
constexpr std::array ENTRIES = {
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
  NativeEntry{0x139C, "LoadSystemSeeds", &LoadSystemSeeds, PRESERVES_ALL},
  NativeEntry{0x13B4, "AdvanceToNextSystem", &AdvanceToNextSystem, PRESERVES_ALL},
  NativeEntry{0x13C1, "GenerateSystemName", &GenerateSystemName,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_SI, 0}},
  NativeEntry{0x14C5, "DrawChartItems", &DrawChartItems, Machine::NativeContract{GENERAL, 0}},
  NativeEntry{0x1505, "PlaceChartLabels", &PlaceChartLabels,
              Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX | REGISTER_DI, 0}},
  NativeEntry{0x1552, "AddChartLabel", &AddChartLabel, Machine::NativeContract{REGISTER_BX | REGISTER_DI, 0}},
  NativeEntry{0x157D, "NudgeChartLabel", &NudgeChartLabel, Machine::NativeContract{0, FLAG_CARRY}},
  NativeEntry{0x15A9, "ChartItemOverlaps", &ChartItemOverlaps, Machine::NativeContract{0, FLAG_CARRY}},
  NativeEntry{0x15BC, "ClearChartTextLines", &ClearChartTextLines, Machine::NativeContract{REGISTER_SI | REGISTER_DI, 0}},
  NativeEntry{0x60EB, "TerminateSelectedSystemName", &TerminateSelectedSystemName, Machine::NativeContract{REGISTER_BX, 0}},
  NativeEntry{0x60F7, "FormatSelectedSystemDistance", &FormatSelectedSystemDistance, Machine::NativeContract{REGISTER_AX | REGISTER_BX, 0}},
  NativeEntry{0x6FC0, "ShowSystemDescription", &ShowSystemDescription, Machine::NativeContract{REGISTER_ALL, 0}},
  NativeEntry{0x700F, "ExpandDescriptionText", &ExpandDescriptionText, Machine::NativeContract{REGISTER_AX | REGISTER_BX | REGISTER_CX, 0}},
  NativeEntry{0x7115, "NextDescriptionRandom", &NextDescriptionRandom, PRESERVES_ALL},
  NativeEntry{0x7124, "CopySelectedNameLower", &CopySelectedNameLower, Machine::NativeContract{REGISTER_AX | REGISTER_SI, 0}},
};

} // namespace

std::span<const NativeEntry> GalaxyEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
