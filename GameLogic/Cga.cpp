#include "pch.h"

#include "Cga.h"

#include "CgaFont.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Elite
{

namespace
{

constexpr std::uint8_t CRTC_INDEX_MASK = 0x1F;

/// The bits each of the Motorola 6845's registers holds.
constexpr std::array<std::uint8_t, Cga::CRTC_REGISTER_COUNT> CRTC_REGISTER_MASKS = {0xFF, 0xFF, 0xFF, 0x0F, 0x7F, 0x1F, 0x7F, 0x7F, 0x03,
                                                                                    0x1F, 0x7F, 0x1F, 0x3F, 0xFF, 0x3F, 0xFF, 0x3F, 0xFF};

constexpr std::uint8_t MODE_CONTROL_BITS = 0x3F;
constexpr std::uint8_t COLOR_SELECT_BITS = 0x3F;

/// The 6845's 14-bit memory address.
constexpr std::uint32_t CRTC_ADDRESS_MASK = 0x3FFF;
constexpr std::uint32_t VIDEO_MEMORY_MASK = Cga::VIDEO_MEMORY_BYTES - 1;

constexpr std::uint32_t TEXT_ROWS = 25;
constexpr std::uint32_t GRAPHICS_BYTES_PER_LINE = 80;
constexpr std::uint32_t GRAPHICS_BANK_BYTES = 0x2000;
constexpr std::uint32_t GRAPHICS_BANK_MASK = GRAPHICS_BANK_BYTES - 1;

constexpr std::uint8_t CURSOR_START_LINE_BITS = 0x1F;
constexpr std::uint8_t CURSOR_END_LINE_BITS = 0x1F;
constexpr std::uint32_t CURSOR_MODE_SHIFT = 5;
constexpr std::uint32_t CURSOR_MODE_BITS = 0x03;
constexpr std::uint32_t CURSOR_MODE_HIDDEN = 0x01;

constexpr std::uint8_t ATTRIBUTE_FOREGROUND = 0x0F;
constexpr std::uint8_t ATTRIBUTE_BLINK = 0x80;
constexpr std::uint8_t BRIGHT_COLOR_OFFSET = 8;

/// Colours 1-3 of the 320x200 palettes, before intensity.
constexpr std::array<std::uint8_t, 3> PALETTE_0 = {2, 4, 6}; // green, red, brown
constexpr std::array<std::uint8_t, 3> PALETTE_1 = {3, 5, 7}; // cyan, magenta, light gray

[[nodiscard]] std::uint8_t VideoByte(Cga::VideoMemory _memory, std::uint32_t _offset) noexcept
{
  return _memory[_offset & VIDEO_MEMORY_MASK];
}

[[nodiscard]] constexpr std::size_t RegisterNumber(CrtcRegister _index) noexcept
{
  return static_cast<std::size_t>(_index);
}

} // namespace

Cga::Cga(const Cycles& _clock) noexcept
  : m_clock(_clock)
{
}

void Cga::WriteCrtc(std::uint8_t _register, std::uint8_t _value) noexcept
{
  const auto index = static_cast<std::uint8_t>(_register & CRTC_INDEX_MASK);
  if (index < RegisterNumber(CrtcRegister::LightPenHigh))
  {
    m_crtc[index] = static_cast<std::uint8_t>(_value & CRTC_REGISTER_MASKS[index]);
  }
}

void Cga::SetModeControl(std::uint8_t _value) noexcept
{
  m_modeControl = static_cast<std::uint8_t>(_value & MODE_CONTROL_BITS);
}

void Cga::SetColorSelect(std::uint8_t _value) noexcept
{
  m_colorSelect = static_cast<std::uint8_t>(_value & COLOR_SELECT_BITS);
}

std::uint8_t Cga::ReadStatus() noexcept
{
  auto status = Status();
  if ((status & STATUS_VERTICAL_SYNC) != 0)
  {
    const Cycles start = RetraceStart();
    if (start == m_retraceSeen)
    {
      status = static_cast<std::uint8_t>(status & ~STATUS_VERTICAL_SYNC);
    }
    m_retraceSeen = start;
  }
  return status;
}

std::uint8_t Cga::Status() const noexcept
{
  const Cycles phase = m_clock % CGA_CYCLES_PER_FRAME;
  const auto line = static_cast<std::uint32_t>(phase / CGA_CYCLES_PER_LINE);
  const Cycles lineCycle = phase % CGA_CYCLES_PER_LINE;

  std::uint8_t status = 0;
  if (line >= CGA_VISIBLE_LINES || lineCycle >= DISPLAY_CYCLES_PER_LINE)
  {
    status = static_cast<std::uint8_t>(status | STATUS_DISPLAY_INACTIVE);
  }

  const std::uint32_t syncLine =
    std::uint32_t{Crtc(CrtcRegister::VerticalSyncPosition)} * (std::uint32_t{Crtc(CrtcRegister::MaximumScanLine)} + 1);
  if (syncLine < CGA_LINES_PER_FRAME)
  {
    const std::uint32_t linesSinceSync = (line + CGA_LINES_PER_FRAME - syncLine) % CGA_LINES_PER_FRAME;
    if (linesSinceSync < VERTICAL_SYNC_LINES)
    {
      status = static_cast<std::uint8_t>(status | STATUS_VERTICAL_SYNC);
    }
  }
  return status;
}

Cycles Cga::RetraceStart() const noexcept
{
  const Cycles phase = m_clock % CGA_CYCLES_PER_FRAME;
  const auto line = static_cast<std::uint32_t>(phase / CGA_CYCLES_PER_LINE);
  const std::uint32_t syncLine =
    std::uint32_t{Crtc(CrtcRegister::VerticalSyncPosition)} * (std::uint32_t{Crtc(CrtcRegister::MaximumScanLine)} + 1);
  if (syncLine >= CGA_LINES_PER_FRAME)
  {
    return NO_EVENT;
  }
  const std::uint32_t linesSinceSync = (line + CGA_LINES_PER_FRAME - syncLine) % CGA_LINES_PER_FRAME;
  if (linesSinceSync >= VERTICAL_SYNC_LINES)
  {
    return NO_EVENT;
  }
  return m_clock - (Cycles{linesSinceSync} * CGA_CYCLES_PER_LINE + m_clock % CGA_CYCLES_PER_LINE);
}

Cycles Cga::NextStatusChangeAt() const noexcept
{
  const Cycles lineCycle = m_clock % CGA_CYCLES_PER_LINE;
  if (lineCycle < DISPLAY_CYCLES_PER_LINE)
  {
    return m_clock + (DISPLAY_CYCLES_PER_LINE - lineCycle);
  }
  return m_clock + (CGA_CYCLES_PER_LINE - lineCycle);
}

std::uint64_t Cga::FrameNumber() const noexcept
{
  return m_clock / CGA_CYCLES_PER_FRAME;
}

void Cga::Render(VideoMemory _memory, FrameImage& _image) const noexcept
{
  if ((m_modeControl & MODE_GRAPHICS) == 0)
  {
    RenderText(_memory, _image);
  }
  else
  {
    RenderGraphics320(_memory, _image);
  }
}

std::uint32_t Cga::StartAddress() const noexcept
{
  return (std::uint32_t{Crtc(CrtcRegister::StartAddressHigh)} << 8) | Crtc(CrtcRegister::StartAddressLow);
}

std::uint32_t Cga::CursorAddress() const noexcept
{
  return (std::uint32_t{Crtc(CrtcRegister::CursorAddressHigh)} << 8) | Crtc(CrtcRegister::CursorAddressLow);
}

bool Cga::CursorShown() const noexcept
{
  const std::uint32_t mode = (std::uint32_t{Crtc(CrtcRegister::CursorStart)} >> CURSOR_MODE_SHIFT) & CURSOR_MODE_BITS;
  return mode != CURSOR_MODE_HIDDEN && (FrameNumber() / CURSOR_BLINK_HALF_PERIOD_FRAMES) % 2 == 0;
}

bool Cga::CursorCoversLine(std::uint32_t _scanLine) const noexcept
{
  const std::uint32_t first = Crtc(CrtcRegister::CursorStart) & CURSOR_START_LINE_BITS;
  const std::uint32_t last = Crtc(CrtcRegister::CursorEnd) & CURSOR_END_LINE_BITS;
  if (first <= last)
  {
    return _scanLine >= first && _scanLine <= last;
  }
  return _scanLine <= last || _scanLine >= first; // the 6845's split cursor
}

void Cga::RenderText(VideoMemory _memory, FrameImage& _image) const noexcept
{
  const bool eightyColumns = (m_modeControl & MODE_TEXT_80_COLUMNS) != 0;
  const std::uint32_t columns = eightyColumns ? 80 : 40;
  const std::uint32_t dotsPerPixel = eightyColumns ? 1 : 2;
  const bool blinkEnabled = (m_modeControl & MODE_BLINK_ENABLE) != 0;
  const bool blinkingCharactersShown = (FrameNumber() / CHARACTER_BLINK_HALF_PERIOD_FRAMES) % 2 == 0;
  const bool cursorShown = CursorShown();
  const std::uint32_t start = StartAddress();
  const std::uint32_t cursor = CursorAddress();

  std::size_t pixel = 0;
  for (std::uint32_t row = 0; row < TEXT_ROWS; ++row)
  {
    for (std::uint32_t scanLine = 0; scanLine < CgaFont::GLYPH_ROWS; ++scanLine)
    {
      const bool cursorLine = cursorShown && CursorCoversLine(scanLine);
      for (std::uint32_t column = 0; column < columns; ++column)
      {
        const std::uint32_t address = (start + row * columns + column) & CRTC_ADDRESS_MASK;
        const std::uint8_t character = VideoByte(_memory, address * 2);
        const std::uint8_t attribute = VideoByte(_memory, address * 2 + 1);
        const auto foreground = static_cast<std::uint8_t>(attribute & ATTRIBUTE_FOREGROUND);
        const auto background = static_cast<std::uint8_t>(blinkEnabled ? (attribute >> 4) & 0x07 : attribute >> 4);

        std::uint8_t dots = CgaFont::Row(character, scanLine);
        if (blinkEnabled && (attribute & ATTRIBUTE_BLINK) != 0 && !blinkingCharactersShown)
        {
          dots = 0;
        }
        if (cursorLine && address == cursor)
        {
          dots = 0xFF;
        }
        for (std::uint32_t dot = 0; dot < CgaFont::GLYPH_COLUMNS; ++dot)
        {
          const std::uint8_t color = (dots & (0x80U >> dot)) != 0 ? foreground : background;
          for (std::uint32_t repeat = 0; repeat < dotsPerPixel; ++repeat)
          {
            _image.pixels[pixel++] = color;
          }
        }
      }
    }
  }
  _image.borderColor = static_cast<std::uint8_t>(m_colorSelect & COLOR_SELECT_COLOR);
}

void Cga::RenderGraphics320(VideoMemory _memory, FrameImage& _image) const noexcept
{
  const auto background = static_cast<std::uint8_t>(m_colorSelect & COLOR_SELECT_COLOR);
  const std::uint8_t brightness = (m_colorSelect & COLOR_SELECT_INTENSITY) != 0 ? BRIGHT_COLOR_OFFSET : 0;
  const std::array<std::uint8_t, 3>& palette = (m_colorSelect & COLOR_SELECT_PALETTE_1) != 0 ? PALETTE_1 : PALETTE_0;
  const std::array<std::uint8_t, 4> colors = {background, static_cast<std::uint8_t>(palette[0] + brightness),
                                              static_cast<std::uint8_t>(palette[1] + brightness),
                                              static_cast<std::uint8_t>(palette[2] + brightness)};
  const std::uint32_t start = StartAddress() * 2;

  std::size_t pixel = 0;
  for (std::uint32_t line = 0; line < FrameImage::HEIGHT_PIXELS; ++line)
  {
    const std::uint32_t lineStart = start + (line / 2) * GRAPHICS_BYTES_PER_LINE;
    const std::uint32_t bank = (line % 2) * GRAPHICS_BANK_BYTES;
    for (std::uint32_t column = 0; column < GRAPHICS_BYTES_PER_LINE; ++column)
    {
      const std::uint8_t packed = VideoByte(_memory, ((lineStart + column) & GRAPHICS_BANK_MASK) | bank);
      for (std::uint32_t shift = 8; shift != 0; shift -= 2)
      {
        const std::uint8_t color = colors[(packed >> (shift - 2)) & 0x03];
        _image.pixels[pixel++] = color;
        _image.pixels[pixel++] = color;
      }
    }
  }
  _image.borderColor = background;
}

} // namespace Elite
