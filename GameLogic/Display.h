// GameLogic/Display.h
#pragma once

#include "Timing.h"

#include <array>
#include <cstdint>

namespace Elite
{

/// One colour as a display shows it, 8 bits a channel.
struct RgbColor
{
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;
};

/// The 16 colours the game's own pixels and its colour select name, as the IBM colour display showed them: what the renderer maps
/// the space view's layers and its background through. Colour 6 is brown, not dark yellow.
[[nodiscard]] constexpr std::array<RgbColor, 16> DisplayPalette() noexcept
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

/// What is left of the display once the CGA went (ADR-069): its frame clock, which paces the game's frames, and its colour select,
/// which names the four colours of the space view's pixels and its background.
///
/// The frame clock. Frames are 60 a second, frame n starting at FrameStart(n) (Timing.h), and the vertical retrace is the first
/// RETRACE_MILLISECONDS of each. Paced time's rule (ADR-008 item 2): TakeRetrace reports each retrace to the first call that sees
/// it, and to no later one, so that each wait for a retrace meets the next one, as the drawing between two waits outlasted the
/// retrace on the real machine.
class Display
{
public:
  static constexpr Milliseconds RETRACE_MILLISECONDS = 1;

  // The colour select's bits: the background colour, the bright palette, and palette 1 (cyan, magenta and light gray).
  static constexpr std::uint8_t COLOR_SELECT_COLOR = 0x0F;
  static constexpr std::uint8_t COLOR_SELECT_INTENSITY = 0x10;
  static constexpr std::uint8_t COLOR_SELECT_PALETTE_1 = 0x20;

  /// _clock is the platform's millisecond clock, which must outlive the display.
  explicit Display(const Milliseconds& _clock) noexcept;

  /// Whether a vertical retrace is in progress and no call has seen it yet; it is seen from now on.
  [[nodiscard]] bool TakeRetrace() noexcept;

  /// Whether a vertical retrace is in progress, without the rule.
  [[nodiscard]] bool InRetrace() const noexcept;

  /// The next millisecond after now at which InRetrace() changes: the end of the retrace in progress, or the next one's start.
  [[nodiscard]] Milliseconds NextChangeAt() const noexcept;

  /// The colour select set to _value's low six bits.
  void SetColorSelect(std::uint8_t _value) noexcept;

  [[nodiscard]] std::uint8_t ColorSelect() const noexcept
  {
    return m_colorSelect;
  }

  /// The colours, 0-15 of DisplayPalette, the space view's four pixel values show in, by the colour select as it is now: pixel 0
  /// the background, and 1-3 green, red and brown, or cyan, magenta and light gray, bright with the intensity bit.
  [[nodiscard]] std::array<std::uint8_t, 4> GraphicsColors() const noexcept;

private:
  /// The millisecond the vertical retrace in progress began at, or NO_EVENT if there is none.
  [[nodiscard]] Milliseconds RetraceStart() const noexcept;

  const Milliseconds& m_clock;
  std::uint8_t m_colorSelect = 0;
  Milliseconds m_retraceSeen = NO_EVENT; // the start of the last retrace a call reported
};

} // namespace Elite
