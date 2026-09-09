#include "pch.h"

#include "FlightSession.h"

#include "LoaderScreen.h"
#include "SoundOutput.h"


#include "DockedKeys.h"

#include "ShipBlueprint.h"

namespace Outpost
{

  namespace
  {
    /*
     * What `CIRCLE` would have left in `STP`, for a flight universe that has never drawn one.
     *
     * §6.95: `HFS1` walks a circle `STP` at a time and cannot terminate on a zero, and nothing on
     * the path from a cold start to the first launch was writing one. `CIRCLE` stores 8, 4 or 2 by
     * radius; four is the middle one -- what a planet of ordinary size leaves -- and it is the
     * value the oracle comparison seeds, so the app starts in a state the game could be in rather
     * than in one it could not.
     *
     * **AND IT IS NO LONGER LOAD-BEARING (§6.109).** The routine that writes `STP` on this path is
     * `LAUN`, which was a stub when §6.95 was written: it loads the step with 8, and both `TT110`
     * and `DOENTRY` run it before any circle is drawn. §6.95 diagnosed a missing write as a state
     * the object could not start in, and the honest cause was a routine the port had not built.
     * The seed stays because §6.95's RULE stands -- a default-constructed flight universe is still
     * a state the game cannot be in -- but nothing reads this particular byte before `LAUN` sets
     * it.
     */
    constexpr std::uint8_t LAST_CIRCLE_STEP = 4;

    /*
     * The nine key flags `KY12` to `KY20` at the tail of `RDKEY`, cleared when `QQ11` says
     * this is not a space view.
     *
     * The bomb, the pod, the missiles, the E.C.M., the warp and the docking computer are the keys
     * that DO something rather than steer; with a chart or a market on screen the scan reports them
     * as unheld regardless of the keyboard. `DOKEY`'s six steering keys are deliberately not here.
     */
    constexpr std::size_t NON_STEERING_KEYS[] = {
      Elite::KEY_ENERGY_BOMB, Elite::KEY_ESCAPE_POD, Elite::KEY_ARM_MISSILE,      Elite::KEY_UNARM_MISSILE,  Elite::KEY_FIRE_MISSILE,
      Elite::KEY_ECM,         Elite::KEY_WARP,       Elite::KEY_DOCKING_COMPUTER, Elite::KEY_CANCEL_DOCKING,
    };

    /// The mask that clears sprite 1, which `RDKEY` switches off on its way past and is not
    /// one of the four the sights use. The two `SETL1` values are `Elite::MEMORY_MAP_IO` and
    /// `_RAM`.
    constexpr std::uint8_t RDKEY_SPRITE_MASK = 0b11111101;
  } // namespace

  FlightSession::FlightSession(Window& _window) noexcept
    : m_window(_window)
  {
  }

  void FlightSession::AttachUniverse(Elite::Universe& _universe) noexcept
  {
    m_universe = &_universe;
    /*
     * TWO BYTES THE GAME WOULD HAVE HAD AND A FRESH C++ OBJECT DOES NOT (§6.95).
     *
     * Elite's zero page is a scratchpad rather than a set of variables with owners, so a routine's
     * inputs include everything that ran before it -- and both of these are read on the first
     * launch by code that never writes them. `STP` is above; `XX0` is the blueprint pointer, which
     * part 4 of the flight loop leaves alone for the planet and the sun, so a body inherits
     * whatever the last ship put there (§6.90). The last ship the game drew before the docking bay
     * is the title screen's Cobra Mk III, so that is what the pointer would hold.
     */
    m_universe->heaps.circleStep = LAST_CIRCLE_STEP;
    m_universe->flight.blueprint = Elite::BlueprintOf(Elite::ShipType::CobraMk3);

    // The loader's part 4 -- the sprite positions, sizes and colours the game inherits and
    // never writes. Without it the sights are switched on at (0, 0), off the screen (§6.160).
    Elite::SetUpLoaderVideo(m_universe->video);

    /*
     * XX21+2*SST-2 -- a third byte of the same shape, and this one is not left by a previous
     * screen at all: `BEGIN` writes it at boot and only `NWSPS` writes it afterwards. Zero is what
     * `NWSHP` refuses, so an unseeded session would silently never build a station.
     */
    m_universe->bubble.stationType = Elite::ShipType::Station;

    /*
     * LSO -- and the station's line heap is IT, not a run carved out of `SLSP` (§6.112).
     *
     * `NWSPS` points the station at the sun's 200 bytes, which are `heaps.sun` and not the arena
     * `heap` addresses. Lending the window is what makes the station's lines land somewhere;
     * without it every one of them is written out of range and dropped, and the station you have
     * just launched from is invisible in the rear view.
     */
    m_universe->LendSunHeap();
  }

  void FlightSession::SyncVideoRegisters() noexcept
  {
    /*
     * COMIRQ1's VIC-II half, BOTH PASSES, once per presented frame (slice 4f).
     *
     * The handler runs twice a frame on the real machine -- once at the top of the space view and
     * once at the top of the dashboard -- and each pass programs the registers for the half below
     * it. The port has no raster to interrupt, so it runs the same two passes here and keeps what
     * each of them would have put on the screen; `TickRasterInterrupt` advances `RASTCT` itself, so
     * two calls are a frame however this one is entered.
     *
     * AND THE COUNT MATTERS RATHER THAN BEING TIDY. The bomb test and the colour increment sit
     * ABOVE the split test, so a burning bomb moves the background colour on EVERY pass: running
     * this once a frame would halve the flash rate.
     */
    const std::uint8_t bomb = m_universe->commander.energyBomb;
    const Elite::RasterRegisters first = Elite::TickRasterInterrupt(m_universe->screen, bomb);
    const Elite::RasterRegisters second = Elite::TickRasterInterrupt(m_universe->screen, bomb);

    const Elite::RasterRegisters& spaceView = first.spaceView ? first : second;
    const Elite::RasterRegisters& dashboard = first.spaceView ? second : first;

    // `abraxas` into the memory-pointer register -- &91 is the dashboard's block, which is
    // also the only state in which its rows are multicolour.
    m_universe->canvas.SetDashboardShown(dashboard.memoryPointers == Elite::COLOUR_BANK_DASHBOARD);
    m_universe->canvas.SetBackground(dashboard.background);

    /*
     * moonflower and welcome -- the energy bomb.
     *
     * AND `welcome` IS A COUNTER RATHER THAN A COLOUR, which is why these are stores and not
     * assignments: `COMIRQ1` increments it on every pass while the bomb burns and puts the running
     * count on the bus, so after eight frames of bomb the byte is past 15. `Canvas` is the chip and
     * latches it to four bits, which is where slice 5a put the mask this port had never had.
     */
    m_universe->canvas.SetSpaceViewMulticolour((spaceView.control2 & Elite::BITMAP_MODE_MULTICOLOUR) != 0u);
    m_universe->canvas.SetSpaceViewBackground(spaceView.background);

    // santana and lotus -- the explosion sprite, which is multicolour and red above the
    // split and single-colour in colour 0 below it, so it never draws over the dashboard.
    m_universe->canvas.SetSpriteMulticolour(spaceView.spriteMulticolour, dashboard.spriteMulticolour);
    m_universe->canvas.SetExplosionColour(spaceView.explosionColour, dashboard.explosionColour);
  }

  // ---- the bubble ---------------------------------------------------------------------------------

  /*
   * `SFS1` WAS THE LAST OF THREE ANSWERED HERE AND IS NOT ANY MORE (M4-a-1).
   *
   * `FRS1` and `ANGRY` went in M3-b-1d and are calls the flight loop makes for itself. All three
   * were stubs that said "phase 4" when slice 4a-b answered them, and each carried a comment
   * explaining which answer an empty implementation had to give so that its caller stayed honest.
   * This one outlived the other two by a phase because `SPIN` and `SPIN2` compared the SEQUENCE of
   * calls through it; they answer an `Elite::Drop` now and the comparison is that answer, so the
   * bubble is a real bubble on both sides of the trap.
   */
  // ---- the controls -------------------------------------------------------------------------------

  /*
   * `ScanKeyboard` AND `ScanMatrix` WERE HERE AND ARE NOT ANY MORE (M3-b-3d).
   *
   * `RDKEY` is `Elite::ScanKeyboard` now, over a `Keyboard` that answers one question: is this key
   * down. Everything the routine did around that walk was already the library's -- the `SETL1`
   * bracket is `MemoryMap`, the sprite mask is a `VideoState` write, `ZEKTRAN` is sixty-five bytes
   * of `Universe`, and the `QQ11` tail was the piece this file's own comment called game logic.
   */

  /*
   * `RunDockingComputer` WAS HERE AND IS NOT ANY MORE (M6-0-h-3). The call to `DOCKIT` from
   * `DOKEY`'s `auton` path -- one call to `Elite::RunDockingComputer` over slot 0, which
   * `ReadFlightControls` makes itself now.
   */

} // namespace Outpost
