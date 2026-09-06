#pragma once

#include <cstdint>

#include "Arith.h"
#include "Canvas.h"
#include "Controls.h"
#include "Dashboard.h"
#include "ExtendedTokens.h"
#include "Lasers.h"
#include "LineHeap.h"
#include "Rng.h"
#include "Scanner.h"
#include "ShipDraw.h"
#include "ShipMove.h"
#include "ShipSlot.h"
#include "Stardust.h"
#include "TextPrint.h"
#include "Trumbles.h"

namespace Elite
{

  /*
   * 6502: abraxas, caravanserai and DFLAG -- what the screen is currently set up as.
   *
   * The first two READ LIKE REGISTERS AND ARE NOT. `abraxas` is the value the raster interrupt
   * pokes into `VIC+&18` on its next pass and `caravanserai` the one for `VIC+&11`, so both are
   * ordinary bytes here and only the handler that reads them is hardware. §6.73 made the opposite
   * mistake about `SIGHT`; this is the same question with the answer the other way round.
   *
   * `abraxas` says which block of screen RAM colours the bottom of the screen -- &81 for the one at
   * &6000 and &91 for the one at &6400, which is the dashboard's. `caravanserai` chooses standard
   * or multicolour bitmap for the same half. `DFLAG` is the cheap half of it: non-zero means the
   * dashboard is already on screen, so `wantdials` can skip copying it in again.
   */
  struct ScreenState
  {
    std::uint8_t colourBank = 0x81;  ///< 6502: abraxas
    std::uint8_t bitmapMode = 0xC0;  ///< 6502: caravanserai -- the LOWER half of the screen
    std::uint8_t dashboardShown = 0; ///< 6502: DFLAG

    /// 6502: moonflower -- `caravanserai`'s twin for the upper half, and the energy bomb's whole
    /// effect: flight loop part 3 drops it to %11010000 and the space view goes to standard bitmap
    /// mode for as long as the bomb burns.
    std::uint8_t upperBitmapMode = 0xC0;

    /*
     * 6502: welcome -- the border colour the raster handler cycles while the bomb burns.
     *
     * A table the interrupt indexes rather than a flag: `COMIRQ1` does `LDA welcome,X` and writes
     * VIC register &21, so a non-zero first byte is what makes the background flash. `BOMBOFF`
     * puts it back to zero and `COMIRQ1` increments it, which is the only place it grows.
     */
    std::uint8_t backgroundFlash = 0;

    /*
     * 6502: HFX -- AND IT DOES NOTHING IN THIS VERSION, which took a slice to establish (§6.155).
     *
     * On the BBC and the 6502 Second Processor a non-zero `HFX` makes the hyperspace rings
     * multicoloured, and `IRQ1` is the handler that reads it. This build has neither: upstream's
     * `hfx.asm` is `SKIP 1` and says in as many words that the flag is unused here; `DOHFX` exists
     * as a label with both of its instructions commented out in the original source; the C64's
     * `LL164` is four instructions and does not write it; and the C64's `COMIRQ1` does not read
     * it. Nothing in the assembled game touches this byte except `ZERO`, which clears `FRIN` to
     * `de` and catches it in passing at 1161.
     *
     * So the field is here because the memory is, and the port clears it where the original does.
     * ADR-005 §1 scheduled a per-row shift of the space view for it and there is no such effect to
     * build.
     */
    std::uint8_t hyperspaceEffect = 0;

    /*
     * 6502: RASTCT -- which half of the raster split the interrupt is setting up next.
     *
     * Zero is the space view and one is the dashboard, and `COMIRQ1` reads it as the index into
     * all seven of its tables before writing `innersec,X` back over it. It is the whole of the
     * handler's state: everything else it reads is either a constant table or one of the four
     * bytes above.
     */
    std::uint8_t rasterCounter = 0;
  };

  /*
   * Every byte of game state, owned in one place (Modernize.md §4.4, slice M3-a).
   *
   * Until M3 this was seven structs of references -- `FlightScreen`, `FlightLoop`, `TradeScreen`,
   * `SaveScreen`, `GameStart`, `MissionScreen`, `TitleScreen` -- each an argument list written out
   * because the alternative was a function with twenty parameters, and each a different subset of
   * the same bytes. The bytes themselves lived in two places that had grown apart: `Outpost::Game`
   * in `Main.cpp` held the docked half and `Outpost::FlightSession` the flight half, so which of
   * them owned a byte depended on which screen had needed it first.
   *
   * THIS STRUCT HAS NO REFERENCE MEMBER, NO VIRTUAL AND NO PRINTER, and that is the whole of its
   * design. It copies, so a test can take a snapshot; it has no vtable, so its layout is its
   * fields; and `UniverseImage` can hash it without knowing what else is in the program, which is
   * what `Game::StateHash` (M3-c) and the M0-c replay are built on.
   *
   * The two things that are NOT here are the text machinery and the seams, because both need what
   * this deliberately excludes: `TextPrinter` takes the bell as a `TextEffects*` and
   * `ExtendedTokenPrinter` the control codes as a `ControlCodes*`, so a universe that owned them
   * would own a pointer to the platform. They travel beside it in `Ports` (M3-a-2), which is the
   * struct M3-b collapses to §4.5's four.
   *
   * The order below is §4.4's, which is the order the game's own memory map runs in as nearly as
   * the port's types allow: the drawing scratch, the arena, the screen, the flight, the player.
   */
  struct Universe
  {
    // ---- what the drawing works on ---------------------------------------------------------
    Canvas canvas;
    DrawWorkspace draw;         ///< 6502: SC(1 0) -- the dashboard's cursor, all M2-c left of it
    MathWorkspace math;         ///< 6502: Q and K2's bottom byte, the two that outlive a call
    GeometryWorkspace geometry; ///< 6502: XX16, XX12, XX2 and XX3 -- `LL9`'s stage results

    // ---- the arena ---------------------------------------------------------------------------
    Stardust dust;         ///< 6502: SX, SY, SZ and their fractions, and NOSTM
    PlanetSunState heaps;  ///< 6502: LSO, LSX2, LSY2, LSP, K5, K6, FLAG, STP, SUNX and Yx2M1
    Bubble bubble;         ///< 6502: FRIN, K%, MANY, JUNK and SLSP
    Ship work{};           ///< 6502: INWK -- the ship the loop is working on
    LineHeap heap;         ///< 6502: the `LS%` region, and `SLSP` inside it
    ClipState clip;        ///< 6502: dontclip -- the short-range chart's, which the clipper reads
    Projection projection; ///< 6502: K3 and K4 -- where the last ship landed on screen
    K3Block axes{};        ///< 6502: K3, which `SPS1` fills for the docking check

    // ---- the screen --------------------------------------------------------------------------
    ScreenState screen;   ///< 6502: the border, the bank and the mode bytes
    TextState text;       ///< 6502: XC, YC, QQ17 and COL
    MessageState message; ///< 6502: DLY, de, MCH and messXC

    // 6502: DTW1 to DTW8 -- `ExtendedTextState` is NOT here, because it is a member of
    // `CharacterPrinter` and the printers stay outside a universe that has to copy. Moving it in
    // means giving the printer a reference to it, which is M3-b's question and not M3-a's.
    VideoState video{};         ///< 6502: the VIC-II registers `MVTRIBS` reads back
    TrumbleSprites trumbles;    ///< 6502: TRIBCT, TRIBVX, TRIBVXH, TRIBXH

    std::uint8_t view = 0;      ///< 6502: QQ11 -- which screen is up
    std::uint8_t spaceView = 0; ///< 6502: VIEW -- which way the player is looking, 0 to 3

    // ---- the flight --------------------------------------------------------------------------
    FlightState flight;
    FlightStatus status;
    Compass compass{0xC3u, 0x9Cu, COMPASS_AHEAD};
    LaserBurst burst{};      ///< 6502: LASX and LASY
    KeyLogger keys{};        ///< 6502: KLO
    ControlState control;    ///< 6502: JSTX, JSTY and `auto`
    ControlOptions options;  ///< 6502: DAMP, DJD and JSTK
    std::uint8_t explosions = 0; ///< 6502: EV

    // ---- the player --------------------------------------------------------------------------
    Commander commander;
    Rng rng;

    /*
     * 6502: tek -- the current system's tech level.
     *
     * The flight loop READS it, because part 14 spawns the station and `NWSPS` picks a Coriolis or
     * a Dodo by this byte; the docked half writes it on arrival. It was a reference into
     * `CurrentSystem` while the two halves owned separate memory, and is one byte here because
     * they no longer do. M3-a-2 is where `CurrentSystem` joins it.
     */
    std::uint8_t techLevel = 0;

    /*
     * 6502: LSO -- the sun's heap, which `NWSPS` hands to the SPACE STATION (§6.112).
     *
     * The line heap and the sun heap are separate arrays here and one region in the original, and
     * a station's lines are written through the sun's window. Called once when a universe starts
     * running rather than on every frame, because it is a wiring step and not a per-frame one.
     */
    void LendSunHeap() noexcept
    {
      heap.AttachSunHeap(heaps.sun);
    }
  };

} // namespace Elite
