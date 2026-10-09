#include "pch.h"

#include "Bios.h"

#include "Firmware.h"
#include "Memory.h"
#include "PortBus.h"

#include <array>

namespace Machine
{

namespace
{

using CrtcTable = std::array<std::uint8_t, 16>;

// The IBM PC BIOS's VIDEO_PARMS: CRTC registers 0-15 for 40x25 text (modes 0, 1), 80x25 text (2, 3) and graphics (4,
// 5, 6). Registers 12-15, the start address and the cursor address, are zero.
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
constexpr std::uint16_t CURSOR_SHAPE_DEFAULT = 0x0607;
constexpr std::uint8_t CRTC_CURSOR_HIGH = 0x0E;
constexpr std::uint8_t CRTC_CURSOR_LOW = 0x0F;

constexpr std::uint16_t CRTC_INDEX_PORT = 0x3D4;
constexpr std::uint16_t CRTC_DATA_PORT = 0x3D5;
constexpr std::uint16_t MODE_CONTROL_PORT = 0x3D8;
constexpr std::uint16_t COLOR_SELECT_PORT = 0x3D9;

constexpr std::uint16_t VIDEO_SEGMENT = 0xB800;
constexpr std::uint32_t VIDEO_WORDS = 8192;
constexpr std::uint8_t BLANK_CHARACTER = 0x20;
constexpr std::uint8_t BLANK_ATTRIBUTE = 0x07;
constexpr std::uint8_t LAST_ROW = 24;

constexpr std::uint8_t CHARACTER_BELL = 0x07;
constexpr std::uint8_t CHARACTER_BACKSPACE = 0x08;
constexpr std::uint8_t CHARACTER_LINE_FEED = 0x0A;
constexpr std::uint8_t CHARACTER_CARRIAGE_RETURN = 0x0D;

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

Bios::Bios(Memory& _memory, PortBus& _ports) noexcept
  : m_memory(_memory),
    m_ports(_ports)
{
}

std::optional<FaultKind> Bios::Video(Registers& _regs)
{
  switch (High(_regs.ax))
  {
  case 0x00:
  {
    if (!SetMode(Low(_regs.ax)))
    {
      return FaultKind::UnsupportedRequest;
    }
    _regs.ax = BiosByte(Firmware::COLOR_SELECT);
    return std::nullopt;
  }
  case 0x0B:
  {
    const std::uint8_t which = High(_regs.bx);
    const std::uint8_t value = Low(_regs.bx);
    std::uint8_t colorSelect = BiosByte(Firmware::COLOR_SELECT);
    if (which == 0)
    {
      colorSelect = static_cast<std::uint8_t>((colorSelect & 0xE0) | (value & 0x1F));
    }
    else if (which == 1)
    {
      colorSelect = static_cast<std::uint8_t>((colorSelect & 0xDF) | ((value & 0x01) << 5));
    }
    else
    {
      return FaultKind::UnknownSubfunction;
    }
    m_ports.Out8(COLOR_SELECT_PORT, colorSelect);
    m_memory.Write8(Firmware::DATA_SEGMENT, Firmware::COLOR_SELECT, colorSelect);
    _regs.ax = Word(BiosByte(Firmware::VIDEO_MODE), colorSelect);
    return std::nullopt;
  }
  default:
    return FaultKind::UnknownFunction;
  }
}

std::optional<FaultKind> Bios::Keyboard(Registers& _regs) noexcept
{
  const std::uint8_t function = High(_regs.ax);
  if (function > 0x01)
  {
    return FaultKind::UnknownFunction;
  }
  std::uint16_t head = BiosWord(Firmware::KEYBOARD_HEAD);
  const bool empty = head == BiosWord(Firmware::KEYBOARD_TAIL);
  if (function == 0x01)
  {
    _regs.ax = BiosWord(head);
    _regs.flags = static_cast<std::uint16_t>(empty ? (_regs.flags | FLAG_ZERO) : (_regs.flags & ~FLAG_ZERO));
    return std::nullopt;
  }
  if (empty)
  {
    return FaultKind::WouldBlock;
  }
  _regs.ax = BiosWord(head);
  head = static_cast<std::uint16_t>(head + 2);
  if (head == Firmware::KEYBOARD_BUFFER_END)
  {
    head = Firmware::KEYBOARD_BUFFER;
  }
  m_memory.Write16(Firmware::DATA_SEGMENT, Firmware::KEYBOARD_HEAD, head);
  return std::nullopt;
}

std::optional<FaultKind> Bios::TimeOfDay(Registers& _regs) noexcept
{
  switch (High(_regs.ax))
  {
  case 0x00:
    _regs.ax = BiosByte(Firmware::TIMER_ROLLOVER);
    _regs.dx = BiosWord(Firmware::TIMER_TICKS);
    _regs.cx = BiosWord(Firmware::TIMER_TICKS + 2);
    m_memory.Write8(Firmware::DATA_SEGMENT, Firmware::TIMER_ROLLOVER, 0);
    return std::nullopt;
  case 0x01:
    m_memory.Write16(Firmware::DATA_SEGMENT, Firmware::TIMER_TICKS, _regs.dx);
    m_memory.Write16(Firmware::DATA_SEGMENT, Firmware::TIMER_TICKS + 2, _regs.cx);
    m_memory.Write8(Firmware::DATA_SEGMENT, Firmware::TIMER_ROLLOVER, 0);
    _regs.ax = Low(_regs.ax);
    return std::nullopt;
  default:
    return FaultKind::UnknownFunction;
  }
}

bool Bios::SetMode(std::uint8_t _mode)
{
  if (_mode >= MODE_COUNT)
  {
    return false;
  }
  m_ports.Out8(MODE_CONTROL_PORT, 0x00);
  const CrtcTable& table = CRTC_TABLES[MODE_CRTC_TABLE[_mode]];
  for (std::size_t index = 0; index < table.size(); ++index)
  {
    m_ports.Out8(CRTC_INDEX_PORT, static_cast<std::uint8_t>(index));
    m_ports.Out8(CRTC_DATA_PORT, table[index]);
  }

  const bool graphics = _mode >= FIRST_GRAPHICS_MODE;
  const std::uint16_t fill = graphics ? std::uint16_t{0} : Word(BLANK_ATTRIBUTE, BLANK_CHARACTER);
  for (std::uint32_t index = 0; index < VIDEO_WORDS; ++index)
  {
    m_memory.Write16(VIDEO_SEGMENT, static_cast<std::uint16_t>(index * 2), fill);
  }

  const std::uint8_t modeControl = MODE_CONTROL_VALUES[_mode];
  m_ports.Out8(MODE_CONTROL_PORT, modeControl);
  const std::uint8_t colorSelect = _mode == HIGH_RESOLUTION_MODE ? COLOR_SELECT_HIGH_RESOLUTION : COLOR_SELECT_DEFAULT;
  m_ports.Out8(COLOR_SELECT_PORT, colorSelect);

  constexpr std::uint16_t SEGMENT = Firmware::DATA_SEGMENT;
  m_memory.Write8(SEGMENT, Firmware::VIDEO_MODE, _mode);
  m_memory.Write16(SEGMENT, Firmware::VIDEO_COLUMNS, MODE_COLUMNS[_mode]);
  m_memory.Write16(SEGMENT, Firmware::VIDEO_PAGE_BYTES, MODE_PAGE_BYTES[_mode]);
  m_memory.Write16(SEGMENT, Firmware::VIDEO_START, 0);
  m_memory.Write8(SEGMENT, Firmware::ACTIVE_PAGE, 0);
  for (std::uint16_t page = 0; page < 8; ++page)
  {
    m_memory.Write16(SEGMENT, static_cast<std::uint16_t>(Firmware::CURSOR_POSITIONS + page * 2), 0);
  }
  m_memory.Write16(SEGMENT, Firmware::CURSOR_SHAPE, CURSOR_SHAPE_DEFAULT);
  m_memory.Write16(SEGMENT, Firmware::CRTC_ADDRESS, CRTC_INDEX_PORT);
  m_memory.Write8(SEGMENT, Firmware::MODE_CONTROL, modeControl);
  m_memory.Write8(SEGMENT, Firmware::COLOR_SELECT, colorSelect);
  return true;
}

bool Bios::TeletypeAvailable() const noexcept
{
  return BiosByte(Firmware::VIDEO_MODE) < FIRST_GRAPHICS_MODE;
}

std::uint8_t Bios::CursorColumn() const noexcept
{
  const std::uint8_t page = BiosByte(Firmware::ACTIVE_PAGE);
  return Low(BiosWord(static_cast<std::uint16_t>(Firmware::CURSOR_POSITIONS + page * 2)));
}

bool Bios::WriteTeletype(std::uint8_t _character)
{
  if (!TeletypeAvailable() || _character == CHARACTER_BELL)
  {
    return false;
  }
  const std::uint8_t page = BiosByte(Firmware::ACTIVE_PAGE);
  const std::uint16_t columns = BiosWord(Firmware::VIDEO_COLUMNS);
  const std::uint16_t position = BiosWord(static_cast<std::uint16_t>(Firmware::CURSOR_POSITIONS + page * 2));
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
    const std::uint32_t pageStart = std::uint32_t{BiosWord(Firmware::VIDEO_PAGE_BYTES)} * page;
    const std::uint32_t cell = pageStart + (std::uint32_t{row} * columns + column) * 2u;
    m_memory.Write8(VIDEO_SEGMENT, static_cast<std::uint16_t>(cell), _character);
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
    const std::uint32_t cell = BiosWord(Firmware::VIDEO_START) + (std::uint32_t{row} * columns + column) * 2u;
    ScrollUp(m_memory.Read8(VIDEO_SEGMENT, static_cast<std::uint16_t>(cell + 1)));
  }
  return true;
}

std::uint8_t Bios::BiosByte(std::uint16_t _offset) const noexcept
{
  return m_memory.Read8(Firmware::DATA_SEGMENT, _offset);
}

std::uint16_t Bios::BiosWord(std::uint16_t _offset) const noexcept
{
  return m_memory.Read16(Firmware::DATA_SEGMENT, _offset);
}

void Bios::SetCursor(std::uint8_t _row, std::uint8_t _column)
{
  const std::uint8_t page = BiosByte(Firmware::ACTIVE_PAGE);
  m_memory.Write16(Firmware::DATA_SEGMENT, static_cast<std::uint16_t>(Firmware::CURSOR_POSITIONS + page * 2), Word(_row, _column));

  const std::uint32_t columns = BiosWord(Firmware::VIDEO_COLUMNS);
  const std::uint32_t address = (BiosWord(Firmware::VIDEO_START) + (_row * columns + _column) * 2u) / 2u;
  m_ports.Out8(CRTC_INDEX_PORT, CRTC_CURSOR_HIGH);
  m_ports.Out8(CRTC_DATA_PORT, static_cast<std::uint8_t>((address >> 8) & 0xFF));
  m_ports.Out8(CRTC_INDEX_PORT, CRTC_CURSOR_LOW);
  m_ports.Out8(CRTC_DATA_PORT, static_cast<std::uint8_t>(address & 0xFF));
}

void Bios::ScrollUp(std::uint8_t _attribute) noexcept
{
  const std::uint32_t columns = BiosWord(Firmware::VIDEO_COLUMNS);
  const std::uint32_t start = BiosWord(Firmware::VIDEO_START);
  const std::uint32_t rowBytes = columns * 2u;
  for (std::uint32_t row = 0; row < LAST_ROW; ++row)
  {
    for (std::uint32_t index = 0; index < rowBytes; ++index)
    {
      const std::uint32_t target = start + row * rowBytes + index;
      const std::uint8_t value = m_memory.Read8(VIDEO_SEGMENT, static_cast<std::uint16_t>(target + rowBytes));
      m_memory.Write8(VIDEO_SEGMENT, static_cast<std::uint16_t>(target), value);
    }
  }
  const std::uint32_t lastRow = start + LAST_ROW * rowBytes;
  for (std::uint32_t column = 0; column < columns; ++column)
  {
    m_memory.Write16(VIDEO_SEGMENT, static_cast<std::uint16_t>(lastRow + column * 2u), Word(_attribute, BLANK_CHARACTER));
  }
}

} // namespace Machine
