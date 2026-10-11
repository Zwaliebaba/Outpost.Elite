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
constexpr std::uint8_t LAST_ROW = 24;

constexpr std::uint8_t CHARACTER_BELL = 0x07;
constexpr std::uint8_t CHARACTER_BACKSPACE = 0x08;
constexpr std::uint8_t CHARACTER_LINE_FEED = 0x0A;
constexpr std::uint8_t CHARACTER_CARRIAGE_RETURN = 0x0D;

// The keyboard buffer's place in the data area: KB_BUFFER to KB_BUFFER_END, and its head and tail are offsets from 0040:0000.
constexpr std::uint16_t KEYBOARD_BUFFER = 0x1E;
constexpr std::uint16_t KEYBOARD_BUFFER_END = 0x3E;

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
    m_keyboardHead(KEYBOARD_BUFFER),
    m_keyboardTail(KEYBOARD_BUFFER),
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

void Bios::Store(std::uint8_t& _field, std::uint8_t _value) noexcept
{
  if (_field != _value)
  {
    _field = _value;
    ++m_changes;
  }
}

void Bios::Store(std::uint16_t& _field, std::uint16_t _value) noexcept
{
  if (Low(_field) != Low(_value))
  {
    ++m_changes;
  }
  if (High(_field) != High(_value))
  {
    ++m_changes;
  }
  _field = _value;
}

void Bios::WriteCrtc(std::uint8_t _register, std::uint8_t _value) noexcept
{
  m_cga.WriteCrtc(_register, _value);
  m_portWrites += 2; // OUT 3D4h with the register, then OUT 3D5h with the value
}

bool Bios::SetMode(std::uint8_t _mode)
{
  if (_mode >= MODE_COUNT)
  {
    return false;
  }
  m_cga.SetModeControl(0x00);
  ++m_portWrites;
  const CrtcTable& table = CRTC_TABLES[MODE_CRTC_TABLE[_mode]];
  for (std::size_t index = 0; index < table.size(); ++index)
  {
    WriteCrtc(static_cast<std::uint8_t>(index), table[index]);
  }

  const bool graphics = _mode >= FIRST_GRAPHICS_MODE;
  const std::uint16_t fill = graphics ? std::uint16_t{0} : Word(BLANK_ATTRIBUTE, BLANK_CHARACTER);
  for (std::uint32_t index = 0; index < VIDEO_WORDS; ++index)
  {
    m_state.SetVideoWord(static_cast<std::uint16_t>(index * 2), fill);
  }

  const std::uint8_t modeControl = MODE_CONTROL_VALUES[_mode];
  m_cga.SetModeControl(modeControl);
  ++m_portWrites;
  const std::uint8_t colorSelect = _mode == HIGH_RESOLUTION_MODE ? COLOR_SELECT_HIGH_RESOLUTION : COLOR_SELECT_DEFAULT;
  m_cga.SetColorSelect(colorSelect);
  ++m_portWrites;

  Store(m_videoMode, _mode);
  Store(m_videoColumns, MODE_COLUMNS[_mode]);
  Store(m_videoPageBytes, MODE_PAGE_BYTES[_mode]);
  Store(m_videoStart, 0);
  Store(m_activePage, 0);
  for (std::uint16_t& position : m_cursorPositions)
  {
    Store(position, 0);
  }
  Store(m_cursorShape, CURSOR_SHAPE_DEFAULT);
  Store(m_crtcAddress, CRTC_INDEX_PORT);
  Store(m_modeControl, modeControl);
  Store(m_colorSelect, colorSelect);
  return true;
}

void Bios::SelectPalette(std::uint8_t _palette)
{
  const auto colorSelect = static_cast<std::uint8_t>((m_colorSelect & COLOR_SELECT_PALETTE_KEEP) | ((_palette & 0x01) << 5));
  m_cga.SetColorSelect(colorSelect);
  ++m_portWrites;
  Store(m_colorSelect, colorSelect);
}

bool Bios::TeletypeAvailable() const noexcept
{
  return m_videoMode < FIRST_GRAPHICS_MODE;
}

std::uint8_t Bios::CursorColumn() const noexcept
{
  return Low(m_cursorPositions[m_activePage % CURSOR_PAGES]);
}

bool Bios::WriteTeletype(std::uint8_t _character)
{
  if (!TeletypeAvailable() || _character == CHARACTER_BELL)
  {
    return false;
  }
  const std::uint8_t page = m_activePage;
  const std::uint16_t columns = m_videoColumns;
  const std::uint16_t position = m_cursorPositions[page % CURSOR_PAGES];
  std::uint8_t row = High(position);
  std::uint8_t column = Low(position);

  bool scroll = false;
  switch (_character)
  {
  case CHARACTER_BACKSPACE:
    if (column > 0)
    {
      --column;
    }
    break;
  case CHARACTER_CARRIAGE_RETURN:
    column = 0;
    break;
  case CHARACTER_LINE_FEED:
    scroll = row >= LAST_ROW;
    row = scroll ? LAST_ROW : static_cast<std::uint8_t>(row + 1);
    break;
  default:
  {
    const std::uint32_t pageStart = std::uint32_t{m_videoPageBytes} * page;
    const std::uint32_t cell = pageStart + (std::uint32_t{row} * columns + column) * 2u;
    m_state.SetVideoByte(static_cast<std::uint16_t>(cell), _character);
    ++column;
    if (column >= columns)
    {
      column = 0;
      scroll = row >= LAST_ROW;
      row = scroll ? LAST_ROW : static_cast<std::uint8_t>(row + 1);
    }
    break;
  }
  }

  SetCursor(row, column);
  if (scroll)
  {
    // As WRITE_TTY: the cursor is placed first, and the new line takes the attribute found under it.
    const std::uint32_t cell = m_videoStart + (std::uint32_t{row} * columns + column) * 2u;
    ScrollUp(m_state.VideoByte(static_cast<std::uint16_t>(cell + 1)));
  }
  return true;
}

void Bios::SetCursor(std::uint8_t _row, std::uint8_t _column) noexcept
{
  Store(m_cursorPositions[m_activePage % CURSOR_PAGES], Word(_row, _column));

  const std::uint32_t columns = m_videoColumns;
  const std::uint32_t address = (m_videoStart + (_row * columns + _column) * 2u) / 2u;
  WriteCrtc(CRTC_CURSOR_HIGH, static_cast<std::uint8_t>((address >> 8) & 0xFF));
  WriteCrtc(CRTC_CURSOR_LOW, static_cast<std::uint8_t>(address & 0xFF));
}

void Bios::ScrollUp(std::uint8_t _attribute) noexcept
{
  const std::uint32_t columns = m_videoColumns;
  const std::uint32_t start = m_videoStart;
  const std::uint32_t rowBytes = columns * 2u;
  for (std::uint32_t row = 0; row < LAST_ROW; ++row)
  {
    for (std::uint32_t index = 0; index < rowBytes; ++index)
    {
      const std::uint32_t target = start + row * rowBytes + index;
      const std::uint8_t value = m_state.VideoByte(static_cast<std::uint16_t>(target + rowBytes));
      m_state.SetVideoByte(static_cast<std::uint16_t>(target), value);
    }
  }
  const std::uint32_t lastRow = start + LAST_ROW * rowBytes;
  for (std::uint32_t column = 0; column < columns; ++column)
  {
    m_state.SetVideoWord(static_cast<std::uint16_t>(lastRow + column * 2u), Word(_attribute, BLANK_CHARACTER));
  }
}

std::uint16_t& Bios::KeyAt(std::uint16_t _offset) noexcept
{
  return m_keyboardBuffer[((_offset - KEYBOARD_BUFFER) / 2u) % KEYBOARD_BUFFER_WORDS];
}

std::uint16_t Bios::KeyAt(std::uint16_t _offset) const noexcept
{
  return m_keyboardBuffer[((_offset - KEYBOARD_BUFFER) / 2u) % KEYBOARD_BUFFER_WORDS];
}

std::optional<std::uint16_t> Bios::PeekKey() const noexcept
{
  if (m_keyboardHead == m_keyboardTail)
  {
    return std::nullopt;
  }
  return KeyAt(m_keyboardHead);
}

std::optional<std::uint16_t> Bios::ReadKey() noexcept
{
  if (m_keyboardHead == m_keyboardTail)
  {
    return std::nullopt;
  }
  const std::uint16_t key = KeyAt(m_keyboardHead);
  auto head = static_cast<std::uint16_t>(m_keyboardHead + 2);
  if (head == KEYBOARD_BUFFER_END)
  {
    head = KEYBOARD_BUFFER;
  }
  Store(m_keyboardHead, head);
  return key;
}

void Bios::InsertKey(std::uint16_t _key) noexcept
{
  const std::uint16_t tail = m_keyboardTail;
  const auto next = static_cast<std::uint16_t>(tail + 2 == KEYBOARD_BUFFER_END ? KEYBOARD_BUFFER : tail + 2);
  Store(KeyAt(tail), _key);
  Store(m_keyboardTail, next);
}

BiosClock Bios::ReadClock() noexcept
{
  const BiosClock clock{Ticks(), m_rolledOver};
  Store(m_rolledOver, 0);
  return clock;
}

void Bios::SetClock(std::uint32_t _ticks) noexcept
{
  Store(m_ticksLow, static_cast<std::uint16_t>(_ticks & 0xFFFF));
  Store(m_ticksHigh, static_cast<std::uint16_t>(_ticks >> 16));
  Store(m_rolledOver, 0);
}

Bios::Tick Bios::CountTick() noexcept
{
  Tick tick{false, false, false};
  Store(m_ticksLow, static_cast<std::uint16_t>(m_ticksLow + 1));
  if (m_ticksLow == 0)
  {
    Store(m_ticksHigh, static_cast<std::uint16_t>(m_ticksHigh + 1));
    tick.carried = true;
  }
  if (m_ticksHigh == TICKS_PER_DAY_HIGH)
  {
    tick.dayCheck = true;
    if (m_ticksLow == TICKS_PER_DAY_LOW)
    {
      // A day has passed: the count starts again from 0, high word first, and the rollover flag that int 1Ah reports is set.
      Store(m_ticksHigh, 0);
      Store(m_ticksLow, 0);
      Store(m_rolledOver, 1);
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
