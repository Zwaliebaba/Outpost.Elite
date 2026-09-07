#pragma once

#include "Window.h"

#include "Canvas.h"
#include "Commander.h"
#include "Charts.h"
#include "Controls.h"
#include "Dashboard.h"
#include "Explosion.h"
#include "ExtendedTokens.h"
#include "Flight.h"
#include "FlightLoop.h"
#include "Lasers.h"
#include "LineHeap.h"
#include "VideoState.h"
#include "PlanetDraw.h"
#include "Rng.h"
#include "Scanner.h"
#include "ShipDraw.h"
#include "ShipMove.h"
#include "ShipSlot.h"
#include "Stardust.h"
#include "StartUp.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "Raster.h"
#include "ViewChange.h"
#include "Music.h"
#include "Ports.h"
#include "Universe.h"
#include "SoundEffects.h"

#include <cstdint>

namespace Outpost
{
  class SoundOutput;

  /*
   * The seams the flight code reaches through, and nothing else since M3-a.
   *
   * `GameShell` is the docked half's answer to the same question and this is the flying half's,
   * separate for one reason: what a shell answers is the PLATFORM -- a window, a presenter, a
   * keyboard -- and most of what this answers is phase 4. Putting them together would hide which
   * stubs are waiting on a machine and which are waiting on a slice.
   *
   * IT OWNS NO GAME STATE AT ALL SINCE M3-a. It held twenty-two members -- the drawing scratch, the
   * arena, the flight model, the controls, the line heap -- because the memory the flight touches
   * had to live somewhere and the docked half owned the rest, so which of the two owned a byte
   * depended on which screen had needed it first. `Elite::Universe` owns every one of them now and
   * the composition root owns the universe, so what is left here is the window, the sound and the
   * raster mode: the platform, which is what a session was always supposed to be.
   *
   * It does not build `Elite::Ports` either, since M3-a-3: the struct grew the docked half's four
   * seams, and eight of its thirteen references are then the shell's and the store's rather than
   * this object's. `Outpost::Game` owns it and lends it back through `AttachPorts`, which is what
   * the one seam below needs that takes a `Ports&` to answer -- `DOCKIT`. `TACTICS` was the other
   * until M3-b-1c, which took the seam away and let `MVEIT` call the routine directly.
   *
   * WHAT IS HONESTLY MISSING, said here rather than left to be found while flying.
   *
   *   - `MVTRIBS` and the whole SID are phase 5, so a flight is silent.
   *   - The laser sights and the Trumbles are VIC-II SPRITES, and the presenter resolves the
   *     bitmap and the two blocks of screen RAM but not the sprite overlay -- so `SIGHT` writes
   *     the pointers and the colour and nothing appears. That is the presenter's gap rather than
   *     a missing routine, and it is the one a player notices first.
   *
   * What DOES work is the frame itself: the controls, the stardust, the dashboard, the planet, the
   * ship renderer and all sixteen parts of `M%`.
   */
  class FlightSession final : public Elite::ShipDrawEffects, public Elite::ControlEffects
  {
  public:
    explicit FlightSession(Window& _window) noexcept;

    /// Every byte the flight works on -- `Game`'s, attached once `Game` exists (M5-e-2) -- and the two
    /// bytes the game would have had that a fresh object does not (§6.95), written here because this
    /// is the first moment there is a universe to write them into.
    void AttachUniverse(Elite::Universe& _universe) noexcept;

    FlightSession(const FlightSession&) = delete;
    FlightSession& operator=(const FlightSession&) = delete;

    /// The universe the routines work on, which is `Game`'s since M5-e-2 -- as is the `Ports&` that
    /// goes beside it in every signature, since M3-a-3.
    [[nodiscard]] Elite::Universe& Universe() noexcept
    {
      return *m_universe;
    }

    /// The seams, lent back by the composition root once it has built them. This object's own
    /// `RunDockingComputer` is a call that needs them, and the interface it satisfies does not
    /// carry them.
    void AttachPorts(Elite::Ports& _ports) noexcept
    {
      m_ports = &_ports;
    }

    /*
     * 6502: comirq1 -- what the raster interrupt does with `abraxas` and `caravanserai` on its way
     * past, which is to poke them into VIC registers &18 and &11.
     *
     * The port keeps those two as ordinary bytes (§6.73 the other way round) and the canvas keeps
     * ONE flag for the pair, because they always move together: with the dashboard shown, rows 18
     * to 24 are multicolour AND coloured from the second block of screen RAM. Nothing in
     * `GameLogic` writes the canvas's flag, so this is the wire between them -- and it belongs on
     * every frame rather than on every screen change, because that is when the handler runs.
     *
     * `moonflower`, `welcome` and `HFX` are the same handler's other three reads -- the energy
     * bomb's bitmap mode, its border flash and the hyperspace tearing. `Canvas::Resolve` has no
     * model for any of them, so they are carried and not shown.
     */
    void SyncVideoRegisters() noexcept;

    /*
     * `SpawnChild` WAS HERE AND IS NOT ANY MORE (M4-a-1).
     *
     * It was one line -- `Elite::SpawnChildShip(...).created` -- over the universe this object
     * already holds, so it was a seam in front of a routine (§6.73). `SPIN` and `SPIN2` answer an
     * `Elite::Drop` now and `Elite::PerformDrop` makes the call, which is the same line inside the
     * library.
     */

    // ---- Elite::ShipDrawEffects -----------------------------------------------------------------

    void DrawPlanetOrSun() override;
    void DrawExplosion() override;

    /*
     * 6502: RDKEY, once, into whichever logger the caller owns.
     *
     * Public because `GameShell` needs it for the title screen and the flight loop's own scan
     * needs it for the flight loop, and they must be the same scan: two implementations of `RDKEY`
     * is §6.59's mistake, and the difference between the two callers is the PRESENT around it
     * rather than anything in here.
     */

    // ---- Elite::ControlEffects ------------------------------------------------------------------

    // `ChartShapes` was answered here rather than removed "because the charts are compared against
    // the shipped game through it (§6.115)". M3-b-1b removed it: the charts draw through the
    // universe's own heaps and the comparison is the chart's pixels.
    void RunDockingComputer(Elite::Ship& _work) override;

    /*
     * `SightEffects` AND `ExplosionEffects` WERE ANSWERED HERE AND ARE NOT ANY MORE (M3-b-3a).
     *
     * Five of the six overrides were already one line into `Elite::ApplyXxx` over
     * `Universe::video`, which is where ADR-005 §1 put the registers. The sixth was `SETL1`, and
     * it stored into `m_rasterMode` -- a byte of this object that nothing in the program read.
     * `Universe::memoryMap` is that byte now, in the library, compared against the game's own.
     */

  private:
    Window& m_window;

    /*
     * Every byte the flight works on, and it is the DOCKED HALF'S TOO (Modernize.md §4.4).
     *
     * A pointer to `Game`'s since M5-e-2, a reference to the composition root's from M3-a until then,
     * and never twenty-two members. The sprite registers went with them
     * -- ADR-005 §1 settled that compositing belongs in `Canvas::Resolve`, so they have to be data
     * rather than private state behind a getter (§6.133, §6.148), and `Video()` is the one line
     * that hands `m_universe->video` to the presenter.
     */
    Elite::Universe* m_universe = nullptr; ///< attached by `AttachUniverse`

    /// 6502: the sound buffer, the music player and the chip they write -- the composition root's,
    /// because the docked half beeps and starts the theme through the shell.

    /// 6502: the seams, null until the composition root attaches them -- see `AttachPorts`.
    Elite::Ports* m_ports = nullptr;
  };

  /*
   * WHAT USED TO BE HERE: `FLIGHT_STEPS_PER_SECOND = 59.826`, the NTSC vertical refresh, with a
   * note arguing that "the flight loop is driven by that refresh and nothing else -- there is no
   * timer in the game".
   *
   * The second half is true and the first is what §6.17 had already disproved: the C64's main loop
   * has no `WSCAN` in it, so it is not driven by the refresh at all, and a frame really takes
   * 48,000 to 86,000 cycles -- twelve to twenty-one a second rather than sixty. The rate now comes
   * from `Outpost::FlightFrameSeconds` in `Presentation.h`, where the measurement it is derived
   * from is written down beside it (§6.114).
   *
   * `GameShell::WaitFrames` still counts PRESENTS, because `DELAY` counts vertical syncs and a
   * present is one; that half of §6.17 was right and is unchanged.
   */

} // namespace Outpost
