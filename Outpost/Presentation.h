#pragma once

#include <array>
#include <cstdint>

namespace Outpost
{

  /*
   * The arithmetic behind the window, kept apart from the window (slice 2e).
   *
   * ADR-005 section 1 asks for an index texture, a palette lookup and an integer scale with
   * black bars. Two of those three are decisions rather than API calls, and decisions can be
   * tested on a machine with no GPU -- so they are here, and `ScreenPresenter` is left with the
   * Direct3D and nothing to get wrong that a test could have caught.
   *
   * That split is not tidiness. Everything in this file is verified by the suite on both legs;
   * everything in `ScreenPresenter.cpp` and `Window.cpp` is verified by compiling. Knowing which
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
   * WHICH IS WHY THE STRUCT IS `Rgb` AND NOT `Colour` (slice 5a). A colour in this game is the
   * INDEX -- `Elite::Colour`, in the library, where the chip's four bits are -- and this is what a
   * modern display has to be told to make that index visible. One name for each and neither
   * borrowing the other's.
   *
   * The order is the VIC-II's own, which is why yellow is 7 and orange is 8 rather than anything
   * an artist would choose.
   */
  struct Rgb
  {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
  };

  inline constexpr std::array<Rgb, 16> C64_PALETTE = {{
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
   * Where the 640x400 image goes inside a client area of _width by _height.
   *
   * ADR-005 section 1: the largest INTEGER factor that fits, centred, black bars around it. Integer
   * because the image is 640 columns of hard-edged pixels and a fractional scale with point
   * sampling gives some of them two screen columns and some three -- which on a screen full of
   * one-pixel lines is not a subtle artefact.
   *
   * The aspect stays 8:5 with square pixels: 640x400 is 320x200 doubled and nothing about the shape
   * of the picture changes (Resolution.md ruling 11.4). ADR-005's 5:4 or 4:3 option is untouched.
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

  [[nodiscard]] Viewport FitPicture(int _clientWidth, int _clientHeight) noexcept;

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
   * SO IT IS MEASURED, by `FlightLoopTests`, which runs the shipped `M%` over a mirrored frame
   * with everything that draws or thinks left untrapped. §6.114 measured three scenes with no
   * planet in them; InputTimer.md T-0 (2026-09-08) measured the crowded end while the interpreter
   * was still in the tree, on the STEADY STATE -- the second of two frames, which draws and erases
   * -- with the planet at a high byte of 0x20, the sun at 0x60 or the station at 0x08 dead ahead,
   * and fighters straight ahead at 0x0C, four wireframe lines each:
   *
   *     occupied slots   what is in them                    cycles a frame   frames a second
   *     0                nothing (§6.114's empty scene)         47,784           21.4
   *     2                planet and sun                         93,974           10.9
   *     2                planet and station                     70,498           14.5
   *     5                planet, sun, three fighters           162,487            6.3
   *     5                planet, station, three fighters       137,740            7.4
   *     10               planet, sun, eight fighters           305,693            3.3
   *     10               planet, station, eight fighters       281,016            3.6
   *
   * THE TABLE BELOW IS THE MIDPOINT OF THE TWO SECOND BODIES at each count, because a bubble has
   * one or the other and the model is keyed by the count alone; it is linear between rows, which
   * the eight-fighter scenes bear out (a fighter costs about 21,000 cycles at 0x0C). The count is
   * what `Game::ShipsInBubble` answers: occupied `FRIN` slots, planet and sun included, so a bubble
   * in play is never below two -- a count of zero or one happens only as the wreckage of a death
   * flies past. Ship AI adds 1% (`TACTICS` runs for one ship a frame); position adds more than
   * count does: the same eight fighters cost 366,000 at 0x04 and 177,000 at 0x30, and a sun or a
   * planet filling the screen at 0x02 costs no more than one at a distance, because the disc is
   * clipped to the same pixels. The counter prices neither the trapped sound calls nor the cycles
   * the VIC-II steals, which is a further 5-10% -- so the port still runs slightly fast, and every
   * one of those errors is in the same direction.
   */
  struct FlightFrameCost
  {
    std::uint8_t ships;   ///< 6502: how many slots of `FRIN` are occupied, planet and sun included
    std::uint32_t cycles; ///< what a frame costs there, measured against the shipped `M%`
  };

  inline constexpr std::array<FlightFrameCost, 4> FLIGHT_FRAME_COSTS = {{
    {0, 47'784},
    {2, 82'236},
    {5, 150'113},
    {10, 293'354},
  }};

  /// How long one flight frame should take with `_ships` slots occupied, in seconds. Linear between
  /// the measured rows and flat outside them; the bubble cannot hold more than the last row.
  [[nodiscard]] double FlightFrameSeconds(std::uint8_t _ships) noexcept;

  /*
   * How long a DOCKED pass takes, and it is two vertical syncs and almost nothing else.
   *
   * 6502: `MLOOP` with `QQ12` set -- the guns cool, `DIALS` is skipped, `LDA QQ11 / AND PATG / LSR A
   * / BCS plus13 / LDY #2 / JSR DELAY` waits TWO vertical syncs unless the view byte is odd AND the
   * author-names option is on -- which only the Data on System screen, at 1, ever is -- the
   * Trumbles breed, `TT17` scans the keyboard, and `TT102` dispatches `thiskey`, which on
   * a pass with no key falls through `TT107`'s countdown and returns. Measured by
   * `FlightLoopTests::TheDockedPassCostsWhatItCosts` (InputTimer.md T-0) with `DELAY` and `WSCAN`
   * trapped, on the status screen with no key held: 4,472 cycles, 4,591 with Trumbles aboard,
   * 2,746 on the long-range chart and up to 5,691 on the short-range chart with a cursor key
   * held. So the pass is 4 ms of work and 40 ms of waiting at PAL, and the docked half runs at
   * a little under HALF THE VERTICAL-SYNC RATE -- 22 passes a second on a PAL machine, 26 on NTSC
   * -- which is what paces the hyperspace countdown and the chart crosshairs. On the one screen
   * where the names lift the wait the same pass runs at 228 a second; that is the original's
   * behaviour and the model follows it, because `Game::StepDocked` says which it was.
   *
   * Until InputTimer.md T-1 builds the simulated vertical blank, the syncs are priced at the NTSC
   * frame the rest of this file and `SoundOutput` use. The port paced the docked half at the
   * flight frame's empty-bubble cost until T-0, which was a stand-in and not a measurement.
   */
  inline constexpr std::uint32_t DOCKED_PASS_CYCLES = 4'472;

  /// 6502: LDY #2 / JSR DELAY -- the two syncs a docked pass waits, unless `QQ11 AND PATG` is odd.
  inline constexpr std::uint8_t DOCKED_PASS_SYNCS = 2;

  /// 6502: TT16's `JSR WSCAN` -- one more sync on a chart pass that moves the crosshairs, so they
  /// step at most once a frame. Recorded here; honoured when T-1's blank exists.
  inline constexpr std::uint8_t CHART_CURSOR_SYNCS = 1;

  /// 6502: the VIC-II's frame on the NTSC machine, in cycles -- 65 cycles a line, 263 lines.
  inline constexpr double NTSC_FRAME_CYCLES = 65.0 * 263.0;

  /// How long one docked pass should take, in seconds, given the syncs the last pass asked `DELAY`
  /// for -- `Game::StepDocked`'s answer, two or none (InputTimer.md T-2).
  [[nodiscard]] double DockedPassSeconds(std::uint8_t _syncs) noexcept;

} // namespace Outpost
