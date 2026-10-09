// Machine/Cga.h
#pragma once

#include "PortBus.h"
#include "Timing.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Machine
{

class Memory;

/// One colour as a display shows it, 8 bits a channel.
struct RgbColor
{
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;
};

/// The CGA's 16 RGBI colours as the IBM colour display shows them, indexed by the 4-bit colour
/// numbers FrameImage holds. Colour 6 is brown, not dark yellow: the display halves green when red
/// and green are on without intensity. The render never produces RGB; the presenter maps indices
/// through this table, or through its own.
[[nodiscard]] constexpr std::array<RgbColor, 16> CgaPalette() noexcept
{
  return {{
    {0x00, 0x00, 0x00}, // 0 black
    {0x00, 0x00, 0xAA}, // 1 blue
    {0x00, 0xAA, 0x00}, // 2 green
    {0x00, 0xAA, 0xAA}, // 3 cyan
    {0xAA, 0x00, 0x00}, // 4 red
    {0xAA, 0x00, 0xAA}, // 5 magenta
    {0xAA, 0x55, 0x00}, // 6 brown
    {0xAA, 0xAA, 0xAA}, // 7 light gray
    {0x55, 0x55, 0x55}, // 8 dark gray
    {0x55, 0x55, 0xFF}, // 9 light blue
    {0x55, 0xFF, 0x55}, // 10 light green
    {0x55, 0xFF, 0xFF}, // 11 light cyan
    {0xFF, 0x55, 0x55}, // 12 light red
    {0xFF, 0x55, 0xFF}, // 13 light magenta
    {0xFF, 0xFF, 0x55}, // 14 yellow
    {0xFF, 0xFF, 0xFF}, // 15 white
  }};
}

/// One frame as the CGA scans it out: the visible area, 640 dots by 200 lines, each dot a 4-bit
/// colour index (0-15) into CgaPalette, and the colour of the border around it. 640 is the CGA's
/// dot clock across the visible part of a line, so the 320-wide modes (40-column text and 320x200
/// graphics) fill two dots per pixel. Lines are top to bottom and dots left to right.
///
/// It is 128,000 bytes: allocate it rather than putting it on the stack.
struct FrameImage
{
  static constexpr std::uint32_t WIDTH_PIXELS = 640;
  static constexpr std::uint32_t HEIGHT_PIXELS = 200;

  std::array<std::uint8_t, std::size_t{WIDTH_PIXELS} * HEIGHT_PIXELS> pixels;
  std::uint8_t borderColor;

  [[nodiscard]] std::uint8_t At(std::uint32_t _x, std::uint32_t _y) const noexcept
  {
    return pixels[std::size_t{_y} * WIDTH_PIXELS + _x];
  }
};

/// The 6845's registers, by number.
enum class CrtcRegister : std::uint8_t
{
  HorizontalTotal,
  HorizontalDisplayed,
  HorizontalSyncPosition,
  SyncWidth,
  VerticalTotal,
  VerticalTotalAdjust,
  VerticalDisplayed,
  VerticalSyncPosition,
  InterlaceMode,
  MaximumScanLine,
  CursorStart,
  CursorEnd,
  StartAddressHigh,
  StartAddressLow,
  CursorAddressHigh,
  CursorAddressLow,
  LightPenHigh,
  LightPenLow
};

/// The IBM Color/Graphics Adapter: its I/O ports 0x3D0-0x3DF, its timing, and turning video memory
/// into a picture.
///
/// Ports. 0x3D0, 0x3D2, 0x3D4 and 0x3D6 are the 6845 CRTC's index register and 0x3D1, 0x3D3, 0x3D5
/// and 0x3D7 its data register, aliased as on the card. Writes to a register keep only the bits the
/// Motorola 6845 has. Of the registers only 14-17 read back (cursor address and light pen); the
/// others, and an index past 17, read as 0. 0x3D8 is the mode-control register and 0x3D9 the
/// colour-select register, both write-only. 0x3DA is the status register (below). 0x3DB and 0x3DC
/// (the light-pen latch) and the rest of the range are accepted and ignored. A port that does not
/// drive the bus reads as 0xFF, as an undriven bus does on the PC.
///
/// Timing. Machine time is the integrator's cycle counter (Timing.h), which the CGA reads on every
/// status read and render; it never reads the host's clock. A frame is CGA_CYCLES_PER_FRAME cycles
/// of 262 lines of 304 cycles, and its phase is clock % CGA_CYCLES_PER_FRAME: at phase 0 the beam
/// is at the left of the first visible line. Lines 0-199 are visible and 200-261 are not. Within a
/// visible line the first DISPLAY_CYCLES_PER_LINE cycles are the 640 displayed dots (640 of the
/// line's 912 dots at three dots a cycle, 213.3 cycles, rounded down) and the remaining 91 are
/// horizontal blanking. The status register reports:
///   - bit 0, display enable inactive: during horizontal blanking and on lines 200-261;
///   - bit 3, vertical sync: for VERTICAL_SYNC_LINES (16, fixed in the 6845) lines starting at
///     line R7 x (R9 + 1), the sync position in character rows times scan lines a row, wrapping
///     into the next frame. With the BIOS's values for mode 4 (R7 = 0x70, R9 = 1) and for modes 0
///     and 1 (R7 = 0x1C, R9 = 7) that is line 224, so sync runs from line 224 to 239. A position
///     at or past line 262 never comes round, and there is then no sync;
///   - bit 2 set and bit 1 clear, the light pen's switch open and no trigger: no pen is fitted;
///   - bits 4-7 set: they are not driven.
/// The reference reads 0x3DA at 0x04C4, 0x05D0 and 0x4602, and each masks the value with
/// `and al, 8` at once, so only bit 3 reaches it.
///
/// Memory. Video memory is the 16 KiB at B8000h in Machine::Memory, which the CPU reads and writes
/// directly; the CGA only reads it, to render. The real card repeats those 16 KiB at BC000h. Memory
/// is flat RAM, so a write there does not reach the CGA; the reference only addresses B800:0000-3FFF.
/// There is no CGA snow.
///
/// Rendering. Render draws the visible area from the registers and video memory as they stand. The
/// geometry is the BIOS's: 25 rows of 8 scan lines in text modes and 200 lines in graphics. Of the
/// CRTC's registers it reads the start address (12-13) and the cursor (10, 11, 14, 15). Mode
/// control (0x3D8) chooses among:
///   - video disabled (bit 3 clear): everything black, the border too;
///   - text (bit 1 clear), 40 columns or with bit 0 80, from the CgaFont glyphs. An attribute's low
///     nibble is the foreground. With blink enabled (bit 5) bits 4-6 are the background and bit 7
///     blinks the character; with it disabled bits 4-7 are the background, so bit 7 gives the bright
///     backgrounds 8-15. The border is colour select's bits 0-3. Colour select's bit 4 and mode
///     control's black-and-white bit do not change an RGBI picture in text modes;
///   - 320x200 four-colour graphics (bit 1 set, bit 4 clear): pixel 0 is colour select's bits 0-3,
///     which is also the border. Pixels 1-3 are green, red and brown, or cyan, magenta and light gray
///     with colour select's bit 5; with mode control's black-and-white bit (bit 2) they are cyan, red
///     and light gray whatever bit 5 says. Colour select's bit 4 makes 1-3 the bright versions;
///   - 640x200 two-colour graphics (bits 1 and 4 set): pixel 0 black and pixel 1 colour select's bits
///     0-3, with a black border.
/// Graphics lines alternate between the banks at B8000h (even lines) and BA000h (odd), 80 bytes a
/// line, the leftmost pixel in a byte's top bits.
///
/// Blinking runs off the frame number, clock / CGA_CYCLES_PER_FRAME. The cursor is shown for 8
/// frames and hidden for 8; a blinking character for 16 and 16, half the cursor's rate. Both are
/// shown in the first half of their period, from frame 0. The cursor is the cell whose address is
/// R14:R15. It covers scan lines R10 to R11 of the cell (bits 0-4 of each), and when R10's start is
/// greater than R11's end it is split, covering lines 0 to R11 and R10 to 7, as on the 6845. It is
/// drawn in the cell's foreground colour. It is off when R10's bits 5-6 are 01, when its address is
/// not a cell on the page (SetTextMode, 0x7D31, sets R14 to 0x0E for this), or when its lines miss
/// the cell's 8. The 6845's own blink modes, 10 and 11 in R10's bits 5-6, are not modelled: they
/// blink at the CGA's rate like 00.
class Cga final : public PortBus
{
public:
  static constexpr std::uint16_t FIRST_PORT = 0x3D0;
  static constexpr std::uint16_t LAST_PORT = 0x3DF;
  static constexpr std::uint16_t CRTC_INDEX_PORT = 0x3D4;
  static constexpr std::uint16_t CRTC_DATA_PORT = 0x3D5;
  static constexpr std::uint16_t MODE_CONTROL_PORT = 0x3D8;
  static constexpr std::uint16_t COLOR_SELECT_PORT = 0x3D9;
  static constexpr std::uint16_t STATUS_PORT = 0x3DA;

  static constexpr std::uint32_t VIDEO_MEMORY_LINEAR = 0xB8000;
  static constexpr std::uint32_t VIDEO_MEMORY_BYTES = 0x4000;
  static constexpr std::uint32_t CRTC_REGISTER_COUNT = 18;

  // Mode control, 0x3D8.
  static constexpr std::uint8_t MODE_TEXT_80_COLUMNS = 0x01;
  static constexpr std::uint8_t MODE_GRAPHICS = 0x02;
  static constexpr std::uint8_t MODE_BLACK_AND_WHITE = 0x04;
  static constexpr std::uint8_t MODE_VIDEO_ENABLE = 0x08;
  static constexpr std::uint8_t MODE_GRAPHICS_640 = 0x10;
  static constexpr std::uint8_t MODE_BLINK_ENABLE = 0x20;

  // Colour select, 0x3D9.
  static constexpr std::uint8_t COLOR_SELECT_COLOR = 0x0F;
  static constexpr std::uint8_t COLOR_SELECT_INTENSITY = 0x10;
  static constexpr std::uint8_t COLOR_SELECT_PALETTE_1 = 0x20;

  // Status, 0x3DA.
  static constexpr std::uint8_t STATUS_DISPLAY_INACTIVE = 0x01;
  static constexpr std::uint8_t STATUS_LIGHT_PEN_SWITCH_OPEN = 0x04;
  static constexpr std::uint8_t STATUS_VERTICAL_SYNC = 0x08;
  static constexpr std::uint8_t STATUS_UNDRIVEN_BITS = 0xF0;

  static constexpr Cycles DISPLAY_CYCLES_PER_LINE = 213;
  static constexpr std::uint32_t VERTICAL_SYNC_LINES = 16;
  static constexpr std::uint64_t CURSOR_BLINK_HALF_PERIOD_FRAMES = 8;
  static constexpr std::uint64_t CHARACTER_BLINK_HALF_PERIOD_FRAMES = 16;

  /// _clock is the machine's cycle counter, which must outlive the CGA.
  explicit Cga(const Cycles& _clock) noexcept;

  [[nodiscard]] std::uint8_t In8(std::uint16_t _port) noexcept override;
  void Out8(std::uint16_t _port, std::uint8_t _value) noexcept override;

  /// Draws the visible area and the border as they are at this moment.
  void Render(const Memory& _memory, FrameImage& _image) const noexcept;

  /// What a read of 0x3DA returns now.
  [[nodiscard]] std::uint8_t Status() const noexcept;

  /// Frames begun since power-on: clock / CGA_CYCLES_PER_FRAME.
  [[nodiscard]] std::uint64_t FrameNumber() const noexcept;

  [[nodiscard]] std::uint8_t ModeControl() const noexcept
  {
    return m_modeControl;
  }

  [[nodiscard]] std::uint8_t ColorSelect() const noexcept
  {
    return m_colorSelect;
  }

  [[nodiscard]] std::uint8_t Crtc(CrtcRegister _index) const noexcept
  {
    return m_crtc[static_cast<std::size_t>(_index)];
  }

private:
  [[nodiscard]] std::uint32_t StartAddress() const noexcept;
  [[nodiscard]] std::uint32_t CursorAddress() const noexcept;
  [[nodiscard]] bool CursorShown() const noexcept;
  [[nodiscard]] bool CursorCoversLine(std::uint32_t _scanLine) const noexcept;

  void RenderText(const Memory& _memory, FrameImage& _image) const noexcept;
  void RenderGraphics320(const Memory& _memory, FrameImage& _image) const noexcept;
  void RenderGraphics640(const Memory& _memory, FrameImage& _image) const noexcept;

  const Cycles& m_clock;
  std::array<std::uint8_t, CRTC_REGISTER_COUNT> m_crtc{};
  std::uint8_t m_crtcIndex = 0;
  std::uint8_t m_modeControl = 0;
  std::uint8_t m_colorSelect = 0;
};

} // namespace Machine
