#include "pch.h"

#include "Bios.h"

#include "Cga.h"
#include "GameState.h"

#include <array>

namespace Elite
{

namespace
{

using CrtcTable = std::array<std::uint8_t, 16>;

// The IBM PC BIOS's VIDEO_PARMS: CRTC registers 0-15 for 40x25 text (modes 0, 1), 80x25 text (2, 3) and graphics (4, 5, 6).
// Registers 12-15, the start address and the cursor address, are zero.
constexpr std::array<CrtcTable, 3> CRTC_TABLES = {{
  {0x38, 0x28, 0x2D, 0x0A, 0x1F, 0x06, 0x19, 0x1C, 0x02, 0x07, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00},
  {0x71, 0x50, 0x5A, 0x0A, 0x1F, 0x06, 0x19, 0x1C, 0x02, 0x07, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00},
  {0x38, 0x28, 0x2D, 0x0A, 0x7F, 0x06, 0x64, 0x70, 0x02, 0x01, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00},
}};

constexpr std::uint8_t MODE_COUNT = 7;
// Per mode 0-6: the mode-control value (the BIOS's M7), the columns (M6), the page size (M5) and the CRTC table.
constexpr std::array<std::uint8_t, MODE_COUNT> MODE_CONTROL_VALUES = {0x2C, 0x28, 0x2D, 0x29, 0x2A, 0x2E, 0x1E};
constexpr std::array<std::uint8_t, MODE_COUNT> MODE_COLUMNS = {40, 40, 80, 80, 40, 40, 80};
constexpr std::array<std::uint16_t, MODE_COUNT> MODE_PAGE_BYTES = {2048, 2048, 4096, 4096, 16384, 16384, 16384};
constexpr std::array<std::uint8_t, MODE_COUNT> MODE_CRTC_TABLE = {0, 0, 1, 1, 2, 2, 2};

constexpr std::uint8_t FIRST_GRAPHICS_MODE = 4;
constexpr std::uint8_t HIGH_RESOLUTION_MODE = 6;
constexpr std::uint8_t COLOR_SELECT_DEFAULT = 0x30;
constexpr std::uint8_t COLOR_SELECT_HIGH_RESOLUTION = 0x3F;
constexpr std::uint8_t COLOR_SELECT_PALETTE_KEEP = 0xDF; // all but bit 5, the palette
constexpr std::uint16_t CURSOR_SHAPE_DEFAULT = 0x0607;
constexpr std::uint16_t CRTC_INDEX_PORT = 0x3D4;
constexpr std::uint8_t CRTC_CURSOR_HIGH = 0x0E;
constexpr std::uint8_t CRTC_CURSOR_LOW = 0x0F;

// What POST leaves in the data area for mode 3, before DOS sets it again.
constexpr std::uint8_t POST_MODE_CONTROL = 0x29;

constexpr std::uint32_t VIDEO_WORDS = 8192;
constexpr std::uint8_t BLANK_CHARACTER = 0x20;
constexpr std::uint8_t BLANK_ATTRIBUTE = 0x07;

// The timer handler's day: 1800B0h ticks, its high word 18h and its low B0h.
constexpr std::uint16_t TICKS_PER_DAY_HIGH = 0x18;
constexpr std::uint16_t TICKS_PER_DAY_LOW = 0xB0;

// The PC's timer input is the 14.31818 MHz crystal divided by 12, and the BIOS counts one tick per 65,536 of those.
constexpr std::uint64_t TIMER_DIVISOR = 65536;
constexpr std::uint64_t CENTISECONDS_PER_SECOND = 100;

constexpr std::uint8_t High(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word >> 8);
}

constexpr std::uint8_t Low(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word & 0xFF);
}

constexpr std::uint16_t Word(std::uint8_t _high, std::uint8_t _low) noexcept
{
  return static_cast<std::uint16_t>((_high << 8) | _low);
}

} // namespace

Bios::Bios(GameState& _state, Cga& _cga, std::uint32_t _timerTicks)
  : m_state(_state),
    m_cga(_cga),
    m_videoMode(POWER_ON_VIDEO_MODE),
    m_videoColumns(MODE_COLUMNS[POWER_ON_VIDEO_MODE]),
    m_videoPageBytes(MODE_PAGE_BYTES[POWER_ON_VIDEO_MODE]),
    m_cursorShape(CURSOR_SHAPE_DEFAULT),
    m_crtcAddress(CRTC_INDEX_PORT),
    m_modeControl(POST_MODE_CONTROL),
    m_colorSelect(COLOR_SELECT_DEFAULT),
    m_ticksLow(static_cast<std::uint16_t>(_timerTicks & 0xFFFF)),
    m_ticksHigh(static_cast<std::uint16_t>(_timerTicks >> 16))
{
  SetMode(POWER_ON_VIDEO_MODE);
}

bool Bios::SetMode(std::uint8_t _mode)
{
  if (_mode >= MODE_COUNT)
  {
    return false;
  }
  m_cga.SetModeControl(0x00);
  const CrtcTable& table = CRTC_TABLES[MODE_CRTC_TABLE[_mode]];
  for (std::size_t index = 0; index < table.size(); ++index)
  {
    m_cga.WriteCrtc(static_cast<std::uint8_t>(index), table[index]);
  }

  const bool graphics = _mode >= FIRST_GRAPHICS_MODE;
  const std::uint16_t fill = graphics ? std::uint16_t{0} : Word(BLANK_ATTRIBUTE, BLANK_CHARACTER);
  for (std::uint32_t index = 0; index < VIDEO_WORDS; ++index)
  {
    m_state.SetVideoWord(static_cast<std::uint16_t>(index * 2), fill);
  }

  const std::uint8_t modeControl = MODE_CONTROL_VALUES[_mode];
  m_cga.SetModeControl(modeControl);
  const std::uint8_t colorSelect = _mode == HIGH_RESOLUTION_MODE ? COLOR_SELECT_HIGH_RESOLUTION : COLOR_SELECT_DEFAULT;
  m_cga.SetColorSelect(colorSelect);

  m_videoMode = _mode;
  m_videoColumns = MODE_COLUMNS[_mode];
  m_videoPageBytes = MODE_PAGE_BYTES[_mode];
  m_videoStart = 0;
  m_activePage = 0;
  for (std::uint16_t& position : m_cursorPositions)
  {
    position = 0;
  }
  m_cursorShape = CURSOR_SHAPE_DEFAULT;
  m_crtcAddress = CRTC_INDEX_PORT;
  m_modeControl = modeControl;
  m_colorSelect = colorSelect;
  return true;
}

void Bios::SelectPalette(std::uint8_t _palette)
{
  const auto colorSelect = static_cast<std::uint8_t>((m_colorSelect & COLOR_SELECT_PALETTE_KEEP) | ((_palette & 0x01) << 5));
  m_cga.SetColorSelect(colorSelect);
  m_colorSelect = colorSelect;
}

void Bios::WriteTeletype(std::uint8_t _character)
{
  const std::uint8_t page = m_activePage;
  const std::uint16_t position = m_cursorPositions[page % CURSOR_PAGES];
  const std::uint8_t row = High(position);
  const std::uint8_t column = Low(position);
  const std::uint32_t pageStart = std::uint32_t{m_videoPageBytes} * page;
  const std::uint32_t cell = pageStart + (std::uint32_t{row} * m_videoColumns + column) * 2u;
  m_state.SetVideoByte(static_cast<std::uint16_t>(cell), _character);
  SetCursor(row, static_cast<std::uint8_t>(column + 1));
}

void Bios::SetCursor(std::uint8_t _row, std::uint8_t _column) noexcept
{
  m_cursorPositions[m_activePage % CURSOR_PAGES] = Word(_row, _column);

  const std::uint32_t columns = m_videoColumns;
  const std::uint32_t address = (m_videoStart + (_row * columns + _column) * 2u) / 2u;
  m_cga.WriteCrtc(CRTC_CURSOR_HIGH, static_cast<std::uint8_t>((address >> 8) & 0xFF));
  m_cga.WriteCrtc(CRTC_CURSOR_LOW, static_cast<std::uint8_t>(address & 0xFF));
}

BiosClock Bios::ReadClock() noexcept
{
  const BiosClock clock{Ticks(), m_rolledOver};
  m_rolledOver = 0;
  return clock;
}

void Bios::SetClock(std::uint32_t _ticks) noexcept
{
  m_ticksLow = static_cast<std::uint16_t>(_ticks & 0xFFFF);
  m_ticksHigh = static_cast<std::uint16_t>(_ticks >> 16);
  m_rolledOver = 0;
}

Bios::Tick Bios::CountTick() noexcept
{
  Tick tick{false, false, false};
  m_ticksLow = static_cast<std::uint16_t>(m_ticksLow + 1);
  if (m_ticksLow == 0)
  {
    m_ticksHigh = static_cast<std::uint16_t>(m_ticksHigh + 1);
    tick.carried = true;
  }
  if (m_ticksHigh == TICKS_PER_DAY_HIGH)
  {
    tick.dayCheck = true;
    if (m_ticksLow == TICKS_PER_DAY_LOW)
    {
      // A day has passed: the count starts again from 0, high word first, and the rollover flag that int 1Ah reports is set.
      m_ticksHigh = 0;
      m_ticksLow = 0;
      m_rolledOver = 1;
      tick.dayEnded = true;
    }
  }
  return tick;
}

std::uint32_t Bios::Ticks() const noexcept
{
  return (std::uint32_t{m_ticksHigh} << 16) | m_ticksLow;
}

std::uint32_t Bios::TimerTicksAt(std::uint32_t _centisecondsSinceMidnight) noexcept
{
  const std::uint64_t ticks = std::uint64_t{_centisecondsSinceMidnight} * CRYSTAL_HZ /
                              (CPU_CLOCK_DIVISOR * CYCLES_PER_PIT_TICK * TIMER_DIVISOR * CENTISECONDS_PER_SECOND);
  return ticks < TICKS_PER_DAY ? static_cast<std::uint32_t>(ticks) : TICKS_PER_DAY - 1;
}

} // namespace Elite
