#pragma once

#include "Window.h"

#include "Canvas.h"
#include "Commander.h"
#include "Controls.h"
#include "Dashboard.h"
#include "ExtendedTokens.h"
#include "Flight.h"
#include "FlightLoop.h"
#include "Lasers.h"
#include "LineHeap.h"
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
#include "ViewChange.h"

#include <cstdint>

namespace Outpost
{

  /*
   * The world a flight happens in, and the six seams the flight code reaches through.
   *
   * `GameShell` is the docked half's answer to the same question and this is the flying half's,
   * separate for one reason: what a shell answers is the PLATFORM -- a window, a presenter, a
   * keyboard -- and most of what this answers is phase 4. Putting them together would hide which
   * stubs are waiting on a machine and which are waiting on a slice.
   *
   * IT OWNS THE FLIGHT WORLD AND BORROWS THE SCREEN. The canvas, the text system, the commander,
   * the generator, the flight status, `QQ11` and `EV` all belong to the composition root because
   * the docked screens write them too; everything below `m_draw` is memory only a flight touches,
   * and there is nowhere else for it to live. `Elite::FlightScreen` and `Elite::FlightLoop` are
   * aggregates of references over the two, built once in the constructor rather than per call --
   * they are the original's globals, and globals do not get rebuilt every frame.
   *
   * WHAT IS HONESTLY MISSING, said here rather than left to be found while flying.
   *
   *   - `TACTICS` and `DOCKIT` are phase 4, so nothing in the bubble fights or flies itself.
   *   - `FRS1`, `SFS1` and `ANGRY` are phase 4, so a fired missile never appears and a dying ship
   *     drops no wreckage. `NWSPS` is NOT among them any more: the station is put back on a launch
   *     and near the planet, so `SSPR` is set and `LoopOutcome::Docked` is reachable.
   *   - `DOEXP` is phase 4, so a ship that explodes vanishes instead.
   *   - `MVTRIBS` and the whole SID are phase 5, so a flight is silent.
   *   - The laser sights and the Trumbles are VIC-II SPRITES, and the presenter resolves the
   *     bitmap and the two blocks of screen RAM but not the sprite overlay -- so `SIGHT` writes
   *     the pointers and the colour and nothing appears. That is the presenter's gap rather than
   *     a missing routine, and it is the one a player notices first.
   *
   * What DOES work is the frame itself: the controls, the stardust, the dashboard, the planet, the
   * ship renderer and all sixteen parts of `M%`.
   */
  class FlightSession final : public Elite::FlightLoopEffects,
                              public Elite::ShipEffects,
                              public Elite::ShipDrawEffects,
                              public Elite::ControlEffects,
                              public Elite::SightEffects,
                              public Elite::ViewEffects
  {
  public:
    FlightSession(Window& _window, Elite::Canvas& _canvas, Elite::TextState& _text, Elite::CharacterPrinter& _characters,
                  Elite::TokenPrinter& _printer, Elite::MessageState& _message, Elite::CommanderBlock& _commander, Elite::Rng& _rng,
                  Elite::FlightStatus& _status, std::uint8_t& _view, std::uint8_t& _explosions, std::uint8_t& _techLevel) noexcept;

    FlightSession(const FlightSession&) = delete;
    FlightSession& operator=(const FlightSession&) = delete;

    /// The argument lists the ported routines take. Both are references into this object, so a
    /// caller can hold neither across a destruction and needs to hold neither at all.
    [[nodiscard]] Elite::FlightScreen& Screen() noexcept
    {
      return m_screen;
    }
    [[nodiscard]] Elite::FlightLoop& Loop() noexcept
    {
      return m_loop;
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

    // ---- Elite::FlightLoopEffects, and Elite::DashboardEffects under it -------------------------

    bool PlaySound(std::uint8_t _effect) override;
    bool PlaySoundPitched(std::uint8_t _effect, std::uint8_t _sustain, std::uint8_t _frequency) override;
    void StopSound(std::uint8_t _effect) override;

    void MoveTrumbles() override;
    void StartDockingMusic() override;
    void StopDockingMusic() override;
    [[nodiscard]] bool SpawnAhead(std::uint8_t _type) override;
    void Anger(std::uint8_t _type) override;
    [[nodiscard]] bool SpawnChild(std::uint8_t _aiFlag, std::uint8_t _type) override;

    // ---- Elite::ShipEffects and Elite::ShipDrawEffects ------------------------------------------

    void RunTactics(Elite::ShipBlock& _work) override;
    void DrawPlanetOrSun() override;
    void DrawExplosion() override;
    void SeedExplosionCloud(Elite::LineHeap& _heap, std::uint16_t _address, std::uint16_t _blueprint) override;

    /*
     * 6502: RDKEY, once, into whichever logger the caller owns.
     *
     * Public because `GameShell` needs it for the title screen and `ControlEffects::ScanKeyboard`
     * needs it for the flight loop, and they must be the same scan: two implementations of `RDKEY`
     * is §6.59's mistake, and the difference between the two callers is the PRESENT around it
     * rather than anything in here.
     */
    [[nodiscard]] Elite::TitleKey ScanMatrix(Elite::KeyLogger& _keys) noexcept;

    // ---- Elite::ControlEffects ------------------------------------------------------------------

    void ScanKeyboard() override;
    void RunDockingComputer(Elite::ShipBlock& _work) override;

    // ---- Elite::SightEffects and Elite::ViewEffects ----------------------------------------------

    void SetRasterMode(std::uint8_t _mode) override;
    void SetSightColour(std::uint8_t _colour) override;
    void SetSpritesEnabled(std::uint8_t _mask) override;
    void MaskSprites(std::uint8_t _mask) override;
    void SetPalette(std::uint8_t _colour) override;

  private:
    Window& m_window;
    Elite::Canvas& m_canvas;

    // ---- the flight world -------------------------------------------------------------------------

    Elite::DrawWorkspace m_draw;
    Elite::MathWorkspace m_math;
    Elite::GeometryWorkspace m_geometry;

    Elite::Stardust m_dust;
    Elite::PlanetSunState m_heaps;
    Elite::Bubble m_bubble;
    Elite::ShipBlock m_work{}; ///< 6502: INWK

    Elite::ScreenState m_screenState;
    Elite::FlightState m_flight;
    Elite::Compass m_compass;
    std::uint8_t m_trumbleSprites = 0; ///< 6502: TRIBCT
    std::uint8_t m_spaceView = 0;      ///< 6502: VIEW -- which way the player is looking

    Elite::KeyLogger m_keys{}; ///< 6502: KLO
    Elite::ControlState m_control;
    Elite::ControlOptions m_options;
    Elite::LaserBurst m_burst{};

    Elite::LineHeap m_heap;
    Elite::ClipState m_clip;
    Elite::Projection m_projection;
    Elite::CompassAxes m_axes{};

    /*
     * 6502: VIC+&15 and VIC+&27 -- the sprite enable mask and sprite 0's colour.
     *
     * Held rather than acted on. `SIGHT` computes the whole mask and part 15 ANDs the register it
     * cannot compute (§6.73's read-modify-write), so the port needs somewhere for the register to
     * be even while nothing draws sprites.
     */
    std::uint8_t m_spriteMask = 0;
    std::uint8_t m_spriteColour = 0;
    std::uint8_t m_rasterMode = 0; ///< 6502: L1M -- what `SETL1` last wrote into the handler

    /// The two aggregates the ported routines take, over everything above and everything borrowed.
    /// Declared last because every reference in them is bound at construction.
    Elite::FlightScreen m_screen;
    Elite::FlightLoop m_loop;
  };

  /*
   * How fast a flight frame runs, for `PlanSteps`.
   *
   * THE SHIPPED BUILD IS THE NTSC RELEASE. `tools/c64_source.py` assembles with `_GMA85_NTSC` set,
   * which is the binary every comparison in this port is made against, and an NTSC C64's vertical
   * refresh is 59.826 Hz rather than PAL's 50.125. The flight loop is driven by that refresh and
   * nothing else -- there is no timer in the game -- so this is the rate, and §6.17's PAL/NTSC note
   * is what makes choosing it a decision rather than a guess.
   *
   * `GameShell::WaitFrames` still counts PRESENTS rather than steps, because `DELAY` counts vertical
   * syncs and a present is one. On a 60 Hz display the two are within a third of a percent; on a
   * 144 Hz one a `DELAY` is two and a half times too short and the flight loop is unaffected. That
   * is the accumulator earning its place.
   */
  inline constexpr double FLIGHT_STEPS_PER_SECOND = 59.826;

} // namespace Outpost
