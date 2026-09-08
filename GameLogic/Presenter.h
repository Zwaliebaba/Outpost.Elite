#pragma once

#include <cstdint>

namespace Elite
{

  /*
   * What the library needs from whatever is drawing the screen (Modernize.md §4.5, slice M3-b-3b).
   *
   * THE SECOND OF THE FOUR PORTS, after `SoundSink`. It is a port and not a call into a routine
   * that now exists, and the test is the one every seam in M3-b has been put to: is there anything
   * behind it that `GameLogic` could do for itself? For the four seams this slice removed there
   * was -- `TT66`, `CLYNS`, `TRADEMODE` and `dn2` are all routines the library has -- and for
   * this one there is not. `DELAY` is a loop around `WSCAN`, and `WSCAN` waits for the raster
   * to reach the bottom of the screen. There is no way to wait for a vertical sync that does
   * not know what a screen is.
   *
   * IT COUNTS FRAMES AND NOT SECONDS, and that is the whole of why the method takes a count. `DELAY`
   * is one of three routines in the C64 build that calls `WSCAN` (§6.17), so fifty of them is one
   * second on a PAL machine and five sixths of one on NTSC. A presenter that implemented this as a
   * wall-clock second would be right on one machine and wrong on the other, and the game would be
   * unable to say which.
   *
   * `TunnelEffects` WAS THE OTHER HALF AND IS THIS ONE SINCE M3-b-3c, and it did not survive the
   * move unchanged: it was ONE method threaded as a nullable pointer through eleven routines, and
   * the pointer was carrying three different answers. `ShowFrame` from the shell was one vertical
   * sync; `ShowFrame` from `Main.cpp`'s own `DeathPacing` was a frame held for as long as the next
   * took to compute; and a null pointer was no present at all. Two of those are what the game asks
   * for and are the two methods below. The third was for the oracle, and is a `Presenter` that does
   * nothing (§8).
   */
  class Presenter
  {
  public:
    virtual ~Presenter() = default;

    /*
     * 6502: DELAY -- wait for `_frames` vertical syncs.
     *
     * Declared once where `LineEntryEffects` and `StartUpEffects` each declared it, and that is not
     * a merge of two things that happened to look alike: there is one `DELAY` in the game and the
     * two interfaces were two views of it, which the executable answered with one method (§8).
     */
    virtual void WaitFrames(std::uint8_t _frames) = 0;

    /*
     * The vertical sync the VIC-II gave a tunnel for free while it drew the next circle.
     *
     * `DELAY` with a count of one, and named for the FRAME rather than the circle because two
     * routines want it: `HFL2` after every circle, which is the pacing §6.109 measured, and
     * `HYPNOISE`'s own one-count delay, which is the same thing for the same length.
     */
    virtual void Present() = 0;

    /*
     * What the death sequence's flight-loop frames are shown with -- see `Die` in `Flight.h`, whose
     * loop this paces, and the point is that the original asks for NO wait between them.
     *
     * It is a second method rather than `Present` because the 6502 waits for nothing between those
     * sixty-five frames: the VIC-II was reading the bitmap the whole time, so each was on screen
     * for exactly as long as the next took to compute -- about 12.7 a second at the measured cost.
     * Paced by vertical sync instead, the sixty-four frames of a death go past in one second and
     * read as a glitch rather than as a death, which is the bug §6.149 found and this signature
     * is what stops it coming back.
     *
     * `_ships` is how many the bubble holds, because the cost of a frame depends on it and the
     * bubble EMPTIES as the wreckage flies past -- so the rate rises through the sequence exactly
     * as the original's did. It is counted here rather than by the presenter because `FRIN`'s
     * zero-terminated list is game state.
     */
    virtual void HoldFlightFrame(std::uint8_t _ships) = 0;

    /*
     * The same again for the title screen's spin, and it is a SECOND hold rather than an argument
     * to the first because the two cost curves are different routines' (M3-b-3d).
     *
     * `TITLE` runs `MVEIT` and `LL9` and comes straight back round -- there is no wait for
     * vertical sync anywhere in it (§6.17) -- so the ship turns at whatever rate a 6510 gets
     * through those two, which is 121,276 cycles once it has arrived and 15,600 while it is
     * still a dot. Presenting once per turn hands that decision to the display instead, and on
     * a 165 Hz panel the ship span twenty times too fast (§6.110).
     *
     * `_distance` is `INWK+7`, the byte `TLL2` walks down -- so the curve is indexed by the same
     * counter the original's cost depended on.
     */
    virtual void HoldTitleFrame(std::uint8_t _distance) = 0;
  };

} // namespace Elite
