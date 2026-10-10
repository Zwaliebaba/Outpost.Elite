#include "pch.h"

#include "Docked.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Text.h"

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

namespace
{

using Machine::REGISTER_AX;
using Machine::REGISTER_BX;
using Machine::REGISTER_DI;

constexpr Machine::NativeContract CLOBBERS_AX_BX_DI{REGISTER_AX | REGISTER_BX | REGISTER_DI, 0};

constexpr std::array ENTRIES = {
  NativeEntry{0x658F, "PrintCreditsOnMessageLine", &PrintCreditsOnMessageLine, PRESERVES_ALL},
  NativeEntry{0x6923, "FormatFuelLightYears", &FormatFuelLightYears, CLOBBERS_AX_BX_DI},
  NativeEntry{0x7C88, "DrawDockedFrame", &DrawDockedFrame, PRESERVES_ALL},
  NativeEntry{0x7CE9, "DrawFrameSides", &DrawFrameSides, PRESERVES_ALL},
  NativeEntry{0x7CF8, "DrawFrameRow", &DrawFrameRow, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> DockedEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
