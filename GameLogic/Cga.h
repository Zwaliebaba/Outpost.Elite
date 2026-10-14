// GameLogic/Cga.h
#pragma once

#include "Timing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Elite
{

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

/// The two modes the game sets: 40x25 colour text for the docked screens, and 320x200 four-colour graphics for flight, the
/// title and the charts.
enum class CgaMode : std::uint8_t
{
  Text40Columns,
  Graphics320
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

/// The IBM Color/Graphics Adapter as the game drives it: its registers, its timing, and turning video memory into a
/// picture. It is the emulated PC's model (Machine::Cga until D33) without its ports: the game and the BIOS write the
/// registers by name (WriteCrtc, SetModeControl, SetColorSelect) and read the status (ReadStatus), and nothing reads the
/// other registers back.
///
/// Registers. A write to a CRTC register keeps only the bits the Motorola 6845 has; the light-pen registers, 16 and 17, take
/// no writes. Mode control (3D8h) and colour select (3D9h) keep their low six bits. These 20 bytes, mode control, colour
/// select and the 18 CRTC registers, are what the game-state digest hashes of the card (StateDigest.h).
///
/// Timing. Time is the platform's millisecond clock (Timing.h, D87), which the CGA reads on every status read and render; it
/// never reads the host's clock. Frames are 60 a second, frame n starting at FrameStart(n), and the vertical retrace is the
/// first RETRACE_MILLISECONDS of each. The status register reports bit 3, vertical sync, and bit 0, display inactive, during
/// the retrace, and nothing else; the game masks the status with `and al, 8` at once, so only bit 3 reaches it.
///
/// Paced time's rule (ADR-008 item 2): ReadStatus reports each vertical retrace to the first read that sees it, and to no
/// later one. The game waits for a retrace by reading the status until bit 3 is set, a level rather than an edge. On the real
/// machine the drawing between two such waits outlasts the retrace, so each wait meets the next one. In paced
/// time the drawing takes no time and the retrace would still be on, so the second wait would not wait at all. So a retrace
/// that has been seen is over for the program, and each wait meets the next one, as it did. Status() is the status without
/// the rule.
///
/// Memory. Video memory is the 16 KiB at B800:0000, which the GameState owns and the game writes directly; the CGA only
/// reads it, to render. There is no CGA snow.
///
/// Rendering. Render draws the visible area from the registers and video memory as they stand. The geometry is the BIOS's:
/// 25 rows of 8 scan lines in text modes and 200 lines in graphics. Of the CRTC's registers it reads the start address
/// (12-13) and the cursor (10, 11, 14, 15). Mode control chooses among:
///   - video disabled (bit 3 clear): everything black, the border too;
///   - text (bit 1 clear), 40 columns or with bit 0 80, from the CgaFont glyphs. An attribute's low nibble is the
///     foreground. With blink enabled (bit 5) bits 4-6 are the background and bit 7 blinks the character; with it disabled
///     bits 4-7 are the background, so bit 7 gives the bright backgrounds 8-15. The border is colour select's bits 0-3.
///     Colour select's bit 4 and mode control's black-and-white bit do not change an RGBI picture in text modes;
///   - 320x200 four-colour graphics (bit 1 set, bit 4 clear): pixel 0 is colour select's bits 0-3, which is also the border.
///     Pixels 1-3 are green, red and brown, or cyan, magenta and light gray with colour select's bit 5; with mode control's
///     black-and-white bit (bit 2) they are cyan, red and light gray whatever bit 5 says. Colour select's bit 4 makes 1-3 the
///     bright versions;
///   - 640x200 two-colour graphics (bits 1 and 4 set): pixel 0 black and pixel 1 colour select's bits 0-3, with a black
///     border.
/// Graphics lines alternate between the banks at B800:0000 (even lines) and B800:2000 (odd), 80 bytes a line, the leftmost
/// pixel in a byte's top bits.
///
/// Blinking runs off the frame number (FrameAt). The cursor is shown for 8 frames and hidden for 8; a
/// blinking character for 16 and 16, half the cursor's rate. Both are shown in the first half of their period, from frame 0.
/// The cursor is the cell whose address is R14:R15. It covers scan lines R10 to R11 of the cell (bits 0-4 of each), and when
/// R10's start is greater than R11's end it is split, covering lines 0 to R11 and R10 to 7, as on the 6845. It is drawn in
/// the cell's foreground colour. It is off when R10's bits 5-6 are 01, when its address is not a cell on the page
/// (SetTextMode sets R14 to 0x0E for this), or when its lines miss the cell's 8. The 6845's own blink modes, 10 and
/// 11 in R10's bits 5-6, are not modelled: they blink at the CGA's rate like 00.
class Cga
{
public:
  static constexpr std::uint32_t VIDEO_MEMORY_BYTES = 0x4000;
  static constexpr std::uint32_t CRTC_REGISTER_COUNT = 18;

  /// Video memory, as Render reads it.
  using VideoMemory = std::span<const std::uint8_t, VIDEO_MEMORY_BYTES>;

  // Mode control, 3D8h.
  static constexpr std::uint8_t MODE_TEXT_80_COLUMNS = 0x01;
  static constexpr std::uint8_t MODE_GRAPHICS = 0x02;
  static constexpr std::uint8_t MODE_BLINK_ENABLE = 0x20;

  // Colour select, 3D9h.
  static constexpr std::uint8_t COLOR_SELECT_COLOR = 0x0F;
  static constexpr std::uint8_t COLOR_SELECT_INTENSITY = 0x10;
  static constexpr std::uint8_t COLOR_SELECT_PALETTE_1 = 0x20;

  // Status, 3DAh.
  static constexpr std::uint8_t STATUS_DISPLAY_INACTIVE = 0x01;
  static constexpr std::uint8_t STATUS_VERTICAL_SYNC = 0x08;

  static constexpr Milliseconds RETRACE_MILLISECONDS = 1;
  static constexpr std::uint64_t CURSOR_BLINK_HALF_PERIOD_FRAMES = 8;
  static constexpr std::uint64_t CHARACTER_BLINK_HALF_PERIOD_FRAMES = 16;

  /// _clock is the platform's millisecond clock, which must outlive the CGA.
  explicit Cga(const Milliseconds& _clock) noexcept;

  /// OUT 3D4h,_register then OUT 3D5h,_value: CRTC register _register, its index taken modulo 32.
  void WriteCrtc(std::uint8_t _register, std::uint8_t _value) noexcept;

  /// OUT 3D8h,_value: the mode control register.
  void SetModeControl(std::uint8_t _value) noexcept;

  /// OUT 3D9h,_value: the colour select register.
  void SetColorSelect(std::uint8_t _value) noexcept;

  /// The card set to _mode, as the BIOS's set-mode left it: the video off, CRTC registers 0-15 from the mode's table (the
  /// start address and the cursor 0), then mode control the mode's value and colour select 30h. Video memory is the game's
  /// to clear.
  void SetMode(CgaMode _mode) noexcept;

  /// IN AL,3DAh: the status, under paced time's rule for the vertical retrace (above).
  [[nodiscard]] std::uint8_t ReadStatus() noexcept;

  /// Draws the visible area of _memory and the border as they are at this moment.
  void Render(VideoMemory _memory, FrameImage& _image) const noexcept;

  /// The colours, 0-15 of CgaPalette, that the 320x200 graphics mode shows its four pixel values in, by the colour select
  /// register as it is now.
  [[nodiscard]] std::array<std::uint8_t, 4> GraphicsColors() const noexcept;

  /// Whether the card shows the 320x200 graphics mode from the start of video memory, where the game draws its screens: so a
  /// byte of video memory is at the place on the screen its offset names.
  [[nodiscard]] bool ShowsGraphicsFromStart() const noexcept;

  /// The status as the card drives it now, without paced time's rule.
  [[nodiscard]] std::uint8_t Status() const noexcept;

  /// The next millisecond after now at which Status() changes: the end of the retrace in progress, or the next one's start.
  [[nodiscard]] Milliseconds NextStatusChangeAt() const noexcept;

  /// The frame in progress (FrameAt).
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
  /// The millisecond the vertical retrace in progress began at, or NO_EVENT if there is none.
  [[nodiscard]] Milliseconds RetraceStart() const noexcept;
  [[nodiscard]] std::uint32_t StartAddress() const noexcept;
  [[nodiscard]] std::uint32_t CursorAddress() const noexcept;
  [[nodiscard]] bool CursorShown() const noexcept;
  [[nodiscard]] bool CursorCoversLine(std::uint32_t _scanLine) const noexcept;

  void RenderText(VideoMemory _memory, FrameImage& _image) const noexcept;
  void RenderGraphics320(VideoMemory _memory, FrameImage& _image) const noexcept;

  const Milliseconds& m_clock;
  std::array<std::uint8_t, CRTC_REGISTER_COUNT> m_crtc{};
  std::uint8_t m_modeControl = 0;
  std::uint8_t m_colorSelect = 0;
  Milliseconds m_retraceSeen = NO_EVENT; // the start of the last retrace a read reported
};

} // namespace Elite
