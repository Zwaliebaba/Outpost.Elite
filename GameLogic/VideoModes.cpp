#include "pch.h"

#include "VideoModes.h"

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
// Per mode 0-6: the mode-control value (the BIOS's M7), the columns (M6) and the CRTC table.
constexpr std::array<std::uint8_t, MODE_COUNT> MODE_CONTROL_VALUES = {0x2C, 0x28, 0x2D, 0x29, 0x2A, 0x2E, 0x1E};
constexpr std::array<std::uint8_t, MODE_COUNT> MODE_COLUMNS = {40, 40, 80, 80, 40, 40, 80};
constexpr std::array<std::uint8_t, MODE_COUNT> MODE_CRTC_TABLE = {0, 0, 1, 1, 2, 2, 2};

constexpr std::uint8_t FIRST_GRAPHICS_MODE = 4;
constexpr std::uint8_t HIGH_RESOLUTION_MODE = 6;
constexpr std::uint8_t COLOR_SELECT_DEFAULT = 0x30;
constexpr std::uint8_t COLOR_SELECT_HIGH_RESOLUTION = 0x3F;
constexpr std::uint8_t COLOR_SELECT_PALETTE_KEEP = 0xDF; // all but bit 5, the palette
constexpr std::uint8_t CRTC_CURSOR_HIGH = 0x0E;
constexpr std::uint8_t CRTC_CURSOR_LOW = 0x0F;

constexpr std::uint32_t VIDEO_WORDS = 8192;
constexpr std::uint8_t BLANK_CHARACTER = 0x20;
constexpr std::uint8_t BLANK_ATTRIBUTE = 0x07;

constexpr std::uint16_t Word(std::uint8_t _high, std::uint8_t _low) noexcept
{
  return static_cast<std::uint16_t>((_high << 8) | _low);
}

} // namespace

VideoModes::VideoModes(GameState& _state, Cga& _cga)
  : m_state(_state),
    m_cga(_cga)
{
  SetMode(POWER_ON_VIDEO_MODE);
}

bool VideoModes::SetMode(std::uint8_t _mode)
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

  m_columns = MODE_COLUMNS[_mode];
  m_cursorRow = 0;
  m_cursorColumn = 0;
  m_colorSelect = colorSelect;
  return true;
}

void VideoModes::SelectPalette(std::uint8_t _palette)
{
  const auto colorSelect = static_cast<std::uint8_t>((m_colorSelect & COLOR_SELECT_PALETTE_KEEP) | ((_palette & 0x01) << 5));
  m_cga.SetColorSelect(colorSelect);
  m_colorSelect = colorSelect;
}

void VideoModes::WriteTeletype(std::uint8_t _character)
{
  // At the cursor on page 0, which starts video memory, then the cursor a column on, there and on the CRTC.
  const std::uint32_t cell = std::uint32_t{m_cursorRow} * m_columns + m_cursorColumn;
  m_state.SetVideoByte(static_cast<std::uint16_t>(cell * 2u), _character);
  m_cursorColumn = static_cast<std::uint8_t>(m_cursorColumn + 1);
  const std::uint32_t address = std::uint32_t{m_cursorRow} * m_columns + m_cursorColumn;
  m_cga.WriteCrtc(CRTC_CURSOR_HIGH, static_cast<std::uint8_t>((address >> 8) & 0xFF));
  m_cga.WriteCrtc(CRTC_CURSOR_LOW, static_cast<std::uint8_t>(address & 0xFF));
}

} // namespace Elite
