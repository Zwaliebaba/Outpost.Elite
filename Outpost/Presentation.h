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

  /*
   * How long one turn of the title screen's ship takes on the machine it was written for.
   *
   * `TITLE` HAS NO FRAME CAP. §6.17's scan found `WSCAN` -- the wait for vertical sync -- called
   * from `DELAY`, `TT16+7` and `FREEZE`, and from nowhere else. `TLL2` runs `MVEIT` and `LL9` and
   * goes straight round again, so the ship turns at whatever rate a 6510 can get through those two
   * and the rate is a CONSEQUENCE rather than a setting. Tie it to the display instead -- one turn
   * per present, which is what the shell did -- and the ship spins seven times too fast on a 60 Hz
   * panel and twenty times on a 165 Hz one (§6.110).
   *
   * AND IT IS NOT ONE NUMBER, because the cost is not one number. `LL9` draws a distant ship as a
   * single dot and a near one as a full wireframe of increasingly long lines, so a turn costs four
   * figures at the start of the ship's approach and six at the end. A single rate picked from the
   * settled cost is right for the ninety percent of the time a player spends watching a ship that
   * has arrived, and makes the arrival itself take eleven seconds instead of four and a half.
   *
   * SO IT IS A MEASURED CURVE, indexed by the byte `TLL2` itself walks. These are the costs
   * `CycleTests::TheTitleScreensLoopCostsWhatItCosts` reads off the shipped `MVEIT` and `LL9` with
   * the ship state `TITLE` sets up, for the Cobra at `BR1`'s distance:
   *
   *     INWK+7    cycles     what LL9 is drawing
   *     96..56    15,600     one dot
   *     48        59,400     a small wireframe, 18 lines
   *     16        58,000     the same, holding
   *     8         81,700     22 lines, and getting longer
   *     1        121,276     25 lines across the middle of the screen
   *
   * WHAT IT IS NOT is a general cost model, and the difference matters. §6.17's other half asks for
   * the FLIGHT loop to be cycle-budgeted and free-running, which needs the cost of an arbitrary
   * frame with an arbitrary number of ships in it; this is one routine's cost with one ship in it,
   * measured rather than modelled, and it stops where the measurement stops. The second title
   * screen's Adder is a different ship at a different distance and is paced by the Cobra's curve,
   * which is wrong by however much the two differ -- and still nearer than a display refresh.
   *
   * It is biased slightly FAST, because the counter does not model the cycles the VIC-II steals
   * from the processor, which on a real machine is a further 5-10%.
   */
  struct TitleTurnCost
  {
    std::uint8_t distanceHigh; ///< 6502: INWK+7, which `TLL2` walks from 96 down to 1
    std::uint32_t cycles;      ///< what one turn costs there, measured against the shipped routines
  };

  inline constexpr std::array<TitleTurnCost, 5> TITLE_TURN_COSTS = {{
    {96, 15'600},
    {56, 15'600},
    {48, 59'400},
    {16, 58'000},
    {1, 121'276},
  }};

  /// 6502: the 6510's clock on the NTSC machine this build is for -- 1,022,727 Hz. The PAL one is
  /// 985,248, and choosing between them is the same decision the shipped build's variant makes.
  inline constexpr double NTSC_CLOCK_HZ = 1'022'727.0;

  /// How long a turn of the title ship should take with the ship `_distanceHigh` away, in seconds.
  /// Linear between the measured points, flat outside them.
  [[nodiscard]] double TitleTurnSeconds(std::uint8_t _distanceHigh) noexcept;

  /*
   * How long a FLIGHT frame takes on the machine it was written for.
   *
   * §6.17 settled the shape of this question in 2026-09-03 and it took until now to answer it. The
   * C64's main loop has no frame cap: `WSCAN` -- the wait for vertical sync -- is called from
   * `DELAY`, `TT16+7` and `FREEZE`, and the `JSR WSCAN` in `main_flight_loop_part_13_of_16` is
   * inside a version gate the C64 build is not in. So `M%` runs as fast as a 6510 gets round it,
   * the rate is a CONSEQUENCE of what a frame costs, and **that is why the real game visibly slows
   * down when the screen fills with ships**.
   *
   * THE PORT RAN IT AT THE VERTICAL REFRESH INSTEAD, and the note that did so argued the loop "is
   * driven by that refresh and nothing else -- there is no timer in the game". The first half is
   * what §6.17 had already disproved; the second is true and is the reason there is no rate to
   * read out of the source. 59.826 frames a second is four to five times what the machine manages,
   * which is a game running at four to five times speed: the station spinning, the ships closing,
   * the fuel burning, all of it (§6.114).
   *
   * SO IT IS MEASURED, by `FlightLoopTests::TheFlightFrameCostsWhatItCosts`, which runs the shipped
   * `M%` over a mirrored frame with everything that draws or thinks left untrapped:
   *
   *     bubble            cycles a frame     frames a second
   *     empty                 47,784               21.4
   *     one ship              86,258               11.9
   *     two ships             75,736               13.5
   *
   * TWO BANDS AND NOT A CURVE, because two of those numbers are one number. A frame with ships in
   * it costs about 81,000 cycles and the two scenes sit 7% either side of that -- the difference
   * between them is what the ships ARE and where, not how many, exactly as the title screen's cost
   * turned out to depend on the ship's size rather than on its distance (§6.110). Reading a trend
   * into a 7% wobble would be inventing one.
   *
   * WHAT IT DOES NOT COVER, stated because a measurement whose limits are not written beside it
   * gets read as a fact. The crowded end is not measured: `Seed`'s third slot is a station and its
   * `TACTICS` does not return untrapped, so a frame with a station or a fight in it is paced at the
   * one-ship cost and is really slower. The scenes have no planet in them, so the floor is
   * optimistic. And the counter prices neither the trapped sound calls nor the cycles the VIC-II
   * steals, which is a further 5-10% -- so the port still runs slightly fast, and every one of
   * those errors is in the same direction.
   */
  struct FlightFrameCost
  {
    std::uint8_t ships;   ///< 6502: how many slots of `FRIN` are occupied
    std::uint32_t cycles; ///< what a frame costs there, measured against the shipped `M%`
  };

  inline constexpr std::array<FlightFrameCost, 2> FLIGHT_FRAME_COSTS = {{
    {0, 47'784},
    {1, 81'000},
  }};

  /// How long one flight frame should take with `_ships` in the bubble, in seconds. Flat above the
  /// last measured point, because a rate beyond the measurement would be a guess wearing a number.
  [[nodiscard]] double FlightFrameSeconds(std::uint8_t _ships) noexcept;

} // namespace Outpost
