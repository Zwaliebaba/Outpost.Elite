#pragma once

#include <array>
#include <cstdint>

namespace Outpost
{

  /*
   * The arithmetic behind the window, kept apart from the window (slice 2e).
   *
   * ADR-005 section 1 asks for a 320x200 index texture, a palette lookup and an integer scale with
   * black bars. Two of those three are decisions rather than API calls, and decisions can be
   * tested on a machine with no GPU -- so they are here, and `CanvasPresenter` is left with the
   * Direct3D and nothing to get wrong that a test could have caught.
   *
   * That split is not tidiness. Everything in this file is verified by the suite on both legs;
   * everything in `CanvasPresenter.cpp` and `Window.cpp` is verified by compiling. Knowing which
   * half a bug can be in is worth the extra header.
   */

  /*
   * The sixteen VIC-II colours, as a modern display should show them.
   *
   * These are Pepto's measured PAL values -- the de facto reference, derived from the chip's own
   * colour-difference outputs rather than from anybody's memory of a television. They are NOT in
   * the game: the C64 names a colour by its index and the hardware decides what that looks like,
   * so this table is the one part of the picture the port has to supply rather than port.
   *
   * The order is the VIC-II's own, which is why yellow is 7 and orange is 8 rather than anything
   * an artist would choose.
   */
  struct Colour
  {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
  };

  inline constexpr std::array<Colour, 16> C64_PALETTE = {{
    {0x00, 0x00, 0x00}, // 0  black
    {0xFF, 0xFF, 0xFF}, // 1  white
    {0x68, 0x37, 0x2B}, // 2  red
    {0x70, 0xA4, 0xB2}, // 3  cyan
    {0x6F, 0x3D, 0x86}, // 4  purple
    {0x58, 0x8D, 0x43}, // 5  green
    {0x35, 0x28, 0x79}, // 6  blue
    {0xB8, 0xC7, 0x6F}, // 7  yellow
    {0x6F, 0x4F, 0x25}, // 8  orange
    {0x43, 0x39, 0x00}, // 9  brown
    {0x9A, 0x67, 0x59}, // 10 light red
    {0x44, 0x44, 0x44}, // 11 dark grey
    {0x6C, 0x6C, 0x6C}, // 12 grey
    {0x9A, 0xD2, 0x84}, // 13 light green
    {0x6C, 0x5E, 0xB5}, // 14 light blue
    {0x95, 0x95, 0x95}, // 15 light grey
  }};

  /// The palette as the shader wants it: sixteen RGBA words, alpha opaque, ready to be a constant
  /// buffer or a 16x1 texture without any per-frame work.
  [[nodiscard]] std::array<std::uint32_t, 16> PaletteAsRgba() noexcept;

  /*
   * Where the 320x200 image goes inside a client area of _width by _height.
   *
   * ADR-005 section 1: the largest INTEGER factor that fits, centred, black bars around it. Integer
   * because the image is 320 columns of hard-edged pixels and a fractional scale with point
   * sampling gives some of them two screen columns and some three -- which on a screen full of
   * one-pixel lines is not a subtle artefact.
   *
   * A client area too small for even 1x still gets 1x rather than nothing: a window being dragged
   * narrow should clip, not go blank, and a zero-sized viewport is a Direct3D error rather than a
   * small picture. A zero or negative client area is possible while minimised and gives a viewport
   * of zero area, which the presenter skips.
   */
  struct Viewport
  {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int scale = 0;

    [[nodiscard]] bool Empty() const noexcept
    {
      return width <= 0 || height <= 0;
    }
    [[nodiscard]] bool operator==(const Viewport&) const = default;
  };

  [[nodiscard]] Viewport FitCanvas(int _clientWidth, int _clientHeight) noexcept;

  /*
   * How many steps to run for the time that has passed, and how much time is left over.
   *
   * ADR-005 section 3: a fixed timestep accumulator, and steps are never silently skipped or
   * doubled. "Never silently" is the whole design -- the count comes back and so does whether it
   * was CLAMPED, so a caller can log a stall instead of the game lurching.
   *
   * The clamp matters more than it looks. Without one, a breakpoint or a laptop lid produces an
   * accumulator holding minutes, and the next call runs thousands of steps with no presentation
   * between them: the game appears to hang and then teleports. With one, it drops the backlog and
   * says so.
   */
  struct StepPlan
  {
    int steps = 0;
    double leftoverSeconds = 0.0;
    bool stalled = false; ///< the backlog was longer than the clamp and the rest was dropped
  };

  /// The most steps one call will ever ask for. Four is enough to ride out a dropped frame at any
  /// plausible rate and short enough that a longer gap is reported rather than absorbed.
  inline constexpr int MAX_STEPS_PER_CALL = 4;

  [[nodiscard]] StepPlan PlanSteps(double _elapsedSeconds, double _accumulatedSeconds, double _stepsPerSecond) noexcept;

} // namespace Outpost
