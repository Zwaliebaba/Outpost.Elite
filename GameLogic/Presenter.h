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
   * was -- `TT66`, `CLYNS`, `TRADEMODE` and `dn2` are all routines the library has -- and for this
   * one there is not. `DELAY` is `LDY #n / .DELY1 JSR WSCAN / DEY / BNE DELY1`, and `WSCAN` waits
   * for the raster to reach the bottom of the screen. There is no way to wait for a vertical sync
   * that does not know what a screen is.
   *
   * IT COUNTS FRAMES AND NOT SECONDS, and that is the whole of why the method takes a count. `DELAY`
   * is one of three routines in the C64 build that calls `WSCAN` (§6.17), so fifty of them is one
   * second on a PAL machine and five sixths of one on NTSC. A presenter that implemented this as a
   * wall-clock second would be right on one machine and wrong on the other, and the game would be
   * unable to say which.
   *
   * `Present` IS M3-b-3c's. `TunnelEffects::ShowFrame` is the same idea under an older name -- the
   * vertical sync the VIC-II gave `HFS2` for free while it drew the next circle -- and it is
   * threaded through ten routines as a nullable `TunnelEffects*` that this port replaces. Doing
   * both here would be two patterns in one slice (rule 8).
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
  };

} // namespace Elite
