#include "pch.h"

#include "FlightSession.h"

#include "LoaderScreen.h"
#include "SoundOutput.h"

#include "Tactics.h"

#include "DockedKeys.h"

#include "ShipBlueprint.h"

namespace Outpost
{

  namespace
  {
    /*
     * 6502: what `CIRCLE` would have left in `STP`, for a flight universe that has never drawn one.
     *
     * §6.95: `HFS1` walks a circle `STP` at a time and cannot terminate on a zero, and nothing on
     * the path from a cold start to the first launch was writing one. `CIRCLE` stores 8, 4 or 2 by
     * radius; four is the middle one -- what a planet of ordinary size leaves -- and it is the
     * value the oracle comparison seeds, so the app starts in a state the game could be in rather
     * than in one it could not.
     *
     * **AND IT IS NO LONGER LOAD-BEARING (§6.109).** The routine that writes `STP` on this path is
     * `LAUN`, which was a stub when §6.95 was written: its `LDA #8` is the step, and both `TT110`
     * and `DOENTRY` run it before any circle is drawn. §6.95 diagnosed a missing write as a state
     * the object could not start in, and the honest cause was a routine the port had not built.
     * The seed stays because §6.95's RULE stands -- a default-constructed flight universe is still a
     * state the game cannot be in -- but nothing reads this particular byte before `LAUN` sets it.
     */
    constexpr std::uint8_t LAST_CIRCLE_STEP = 4;

    /*
     * 6502: the nine `STA KY12` to `STA KY20` at the tail of `RDKEY`, cleared when `QQ11` says
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

    /// 6502: the two values `SETL1` writes into the raster handler, and the sprite bit `RDKEY`
    /// clears on its way past -- %11111101 is sprite 1, which is not one of the four the sights use.
    constexpr std::uint8_t RASTER_MODE_SCANNING = 0b101;
    constexpr std::uint8_t RASTER_MODE_NORMAL = 0b100;
    constexpr std::uint8_t RDKEY_SPRITE_MASK = 0b11111101;
  } // namespace

  FlightSession::FlightSession(Window& _window, Elite::Universe& _universe) noexcept
    : m_window(_window),
      m_universe(_universe)
  {
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
    m_universe.heaps.stp = LAST_CIRCLE_STEP;
    m_universe.flight.blueprint = Elite::BlueprintOf(Elite::ShipType::CobraMk3);

    // 6502: the loader's part 4 -- the sprite positions, sizes and colours the game inherits and
    // never writes. Without it the sights are switched on at (0, 0), off the screen (§6.160).
    Elite::SetUpLoaderVideo(m_universe.video);

    /*
     * 6502: XX21+2*SST-2 -- a third byte of the same shape, and this one is not left by a previous
     * screen at all: `BEGIN` writes it at boot and only `NWSPS` writes it afterwards. Zero is what
     * `NWSHP` refuses, so an unseeded session would silently never build a station.
     */
    m_universe.bubble.stationType = Elite::ShipType::Station;

    /*
     * 6502: LSO -- and the station's line heap is IT, not a run carved out of `SLSP` (§6.112).
     *
     * `NWSPS` points the station at the sun's 200 bytes, which are `heaps.sun` and not the arena
     * `heap` addresses. Lending the window is what makes the station's lines land somewhere;
     * without it every one of them is written out of range and dropped, and the station you have
     * just launched from is invisible in the rear view.
     */
    m_universe.LendSunHeap();
  }

  void FlightSession::SyncVideoRegisters() noexcept
  {
    /*
     * 6502: COMIRQ1's VIC-II half, BOTH PASSES, once per presented frame (slice 4f).
     *
     * The handler runs twice a frame on the real machine -- once at the top of the space view and
     * once at the top of the dashboard -- and each pass programs the registers for the half below
     * it. The port has no raster to interrupt, so it runs the same two passes here and keeps what
     * each of them would have put on the screen; `TickRasterInterrupt` advances `RASTCT` itself, so
     * two calls are a frame however this one is entered.
     *
     * AND THE COUNT MATTERS RATHER THAN BEING TIDY. `BIT BOMB / INC welcome` sits above the split
     * test, so a burning bomb moves the background colour on EVERY pass: running this once a frame
     * would halve the flash rate.
     */
    const std::uint8_t bomb = m_universe.commander.energyBomb; // 6502: BOMB
    const Elite::RasterRegisters first = Elite::TickRasterInterrupt(m_universe.screen, bomb);
    const Elite::RasterRegisters second = Elite::TickRasterInterrupt(m_universe.screen, bomb);

    const Elite::RasterRegisters& spaceView = first.spaceView ? first : second;
    const Elite::RasterRegisters& dashboard = first.spaceView ? second : first;

    // 6502: LDA abraxas / STA VIC+&18 -- and &91 is the dashboard's block, which is also the only
    // state in which its rows are multicolour.
    m_universe.canvas.SetDashboardShown(dashboard.memoryPointers == Elite::COLOUR_BANK_DASHBOARD);
    m_universe.canvas.SetBackground(dashboard.background);

    // 6502: moonflower and welcome -- the energy bomb.
    m_universe.canvas.SetSpaceViewMulticolour((spaceView.control2 & Elite::BITMAP_MODE_MULTICOLOUR) != 0u);
    m_universe.canvas.SetSpaceViewBackground(spaceView.background);

    // 6502: santana and lotus -- the explosion sprite, which is multicolour and red above the
    // split and single-colour in colour 0 below it, so it never draws over the dashboard.
    m_universe.canvas.SetSpriteMulticolour(spaceView.spriteMulticolour, dashboard.spriteMulticolour);
    m_universe.canvas.SetExplosionColour(spaceView.explosionColour, dashboard.explosionColour);
  }

  // ---- the bubble ---------------------------------------------------------------------------------

  /*
   * 6502: SFS1, and it is the LAST of three -- `FRS1` and `ANGRY` were seams here until M3-b-1d
   * and are calls the flight loop makes for itself now.
   *
   * All three were stubs that said "phase 4" when slice 4a-b answered them, and each carried a
   * comment explaining which answer an empty implementation had to give so that its caller stayed
   * honest. Nothing is missing behind them any more: `TACTICS` was the last of that, and M3-b-1c
   * let `MVEIT` call it, so a ship that `ANGRY` makes hostile does something about it. This one
   * stays only because `SPIN` and `SPIN2` compare the SEQUENCE of calls, which is M4-a's to fix.
   */
  bool FlightSession::SpawnChild(std::uint8_t _aiFlag, Elite::ShipType _type)
  {
    // 6502: SFS1 with `INF` at the ship being processed, which is `XSAV`'s slot.
    return Elite::SpawnChildShip(m_universe.bubble, m_universe.work, m_universe.rng, m_universe.flight.slot, m_universe.flight.type,
                                 _aiFlag, _type, m_universe.flight.blueprint)
      .created;
  }

  // ---- the ships ----------------------------------------------------------------------------------

  void FlightSession::DrawPlanetOrSun()
  {
    // 6502: LL25 -- JMP PLANET, taken for a type with bit 7 set. `INWK` is the body and `TYPE`
    // decides which of the two it is, exactly as the tail jump does.
    Elite::DrawPlanetOrSun(m_universe.canvas, m_universe.heaps, m_universe.geometry, m_universe.math, m_universe.clip, m_universe.rng,
                           m_universe.work, m_universe.projection, m_universe.flight.type);
  }

  void FlightSession::DrawExplosion()
  {
    // 6502: LL14's JMP DOEXP -- age the cloud by one frame and draw it, which is how the last
    // frame is erased as well as how this one appears. `INWK` is the exploding ship and `XX3` the
    // vertices `LL9` part 8 projected, which `DOEXP` copies onto the ship's line heap.
    Elite::DrawExplosionCloud(m_universe.canvas, m_universe.math, m_universe.rng, m_universe.work, m_universe.heap, m_universe.geometry,
                              m_universe.bubble, *this);
  }

  // ---- the controls -------------------------------------------------------------------------------

  void FlightSession::ScanKeyboard()
  {
    (void)ScanMatrix(m_universe.keys); // 6502: JSR RDKEY, whose answer `DOKEY` does not read
  }

  Elite::TitleKey FlightSession::ScanMatrix(Elite::KeyLogger& _keys) noexcept
  {
    /*
     * 6502: RDKEY -- the CIA matrix walk, replaced by the window's held-key table (row 145).
     *
     * THE LOGGER IS CLEARED FIRST AND THEN DECREMENTED, not stored into: `JSR ZEKTRAN` zeroes all
     * sixty-five bytes and the scan does `DEC KEYLOOK,X`, so a held key reads 255. Everything
     * downstream tests for non-zero, but `DOKEY` also WRITES this array when the docking computer
     * is flying, and a scan that stored rather than cleared would leave the autopilot's synthetic
     * presses standing for ever.
     */
    m_rasterMode = RASTER_MODE_SCANNING;                 // 6502: LDA #%101 / JSR SETL1
    Elite::ApplyMaskSprites(m_universe.video, RDKEY_SPRITE_MASK); // 6502: AND #%11111101 -- sprite 1 off
    _keys.fill(0u);                                      // 6502: JSR ZEKTRAN

    /*
     * 6502: LDX #&40 / .Rdi1 ... / DEC KEYLOOK,X / STX thiskey / SEC / .Rdi3 DEX / BMI Rdiex.
     *
     * THE WALK COUNTS DOWN and `thiskey` is stored on every hit, so what comes back is the LOWEST
     * numbered key being held rather than the first one found. `TITLE` returns that byte and `BR1`
     * compares it against 39, so the direction of this loop is the difference between "Y" opening
     * the disk menu and not opening it.
     */
    Elite::TitleKey answer;
    for (std::uint8_t key = static_cast<std::uint8_t>(_keys.size()); key-- > 0u;)
    {
      if (m_window.Held(key))
      {
        _keys[key] = 0xFFu; // 6502: DEC KEYLOOK,X, on a byte that has just been zeroed
        answer.pressed = true;
        answer.key = key;
      }
    }

    // 6502: LDA QQ11 / BEQ allkeys -- with anything but the space view up, the nine keys that act
    // rather than steer are forgotten. This is the one piece of `RDKEY` that is game logic, and it
    // stays with the scan because it depends on what the scan found.
    if (m_universe.view != 0u)
    {
      for (const std::size_t index : NON_STEERING_KEYS)
      {
        _keys[index] = 0u;
      }
    }

    /*
     * AND THE STEERING KEYS GO ON A CHART, which is the port's rule and NOT `RDKEY`'s.
     *
     * On a C64 the two sets never collide: `<`, `>`, `X` and `S` steer and the cursor keys move the
     * crosshairs, so `RDKEY` has no reason to drop the steering entries and does not. This port's
     * map is a modern one (ADR-005 §4) and the arrows do both jobs, so one of them has to give way
     * while a chart is up -- and it is the steering, because a chart is the one screen where the
     * arrows are what you aim with. The alternative is a ship that rolls while you read the map.
     *
     * It is here rather than in `KeyMap` because this is where the game itself sorts keys by view,
     * one statement above; and it is marked as the port's own so nobody looks for it in `RDKEY`.
     */
    if (Elite::IsChartView(m_universe.view))
    {
      for (const std::size_t index : {Elite::KEY_ROLL_LEFT, Elite::KEY_ROLL_RIGHT, Elite::KEY_PITCH_UP, Elite::KEY_PITCH_DOWN})
      {
        _keys[index] = 0u;
      }
    }

    m_rasterMode = RASTER_MODE_NORMAL; // 6502: LDA #%100 / JSR SETL1
    return answer;
  }

  void FlightSession::RunDockingComputer(Elite::Ship& _work)
  {
    /*
     * 6502: JSR DOCKIT from `DOKEY`'s `auton` path.
     *
     * It steers by writing `INWK+27` to `INWK+30`, which `DOKEY` then turns into synthetic key
     * presses -- so the autopilot flies the ship through the same key logger the player uses, and
     * nothing downstream can tell them apart.
     *
     * `auton` has already built the block this reads: `ZINF`, a nose vector, and `STA TYPE` with
     * &E0, which is the NEGATIVE type `DOCKIT` tests for to know it is flying the player's ship
     * rather than an NPC's. Slot 0 is what `INF` points at on that path.
     */
    (void)_work;
    if (m_ports != nullptr)
    {
      (void)Elite::RunDockingComputer(m_universe, *m_ports, 0u);
    }
  }

  // ---- the VIC-II ----------------------------------------------------------------------------------

  void FlightSession::SetRasterMode(std::uint8_t _mode)
  {
    // 6502: SETL1 -- self-modifying code inside the interrupt handler (§6.59). The port has no
    // interrupt to modify, so the mode is remembered and nothing reads it yet.
    m_rasterMode = _mode;
  }

  /*
   * The four register seams, one line each into `VideoState`.
   *
   * They were four bodies holding four private members and nothing read them (plan §6.148). The
   * arithmetic did not move -- `Elite::Apply*` is where it is now -- and neither did the
   * read-modify-writes, which are still performed rather than computed, for the reason
   * `SightEffects::MaskSprites` gives.
   */
  void FlightSession::SetSightColour(std::uint8_t _colour)
  {
    Elite::ApplySightColour(m_universe.video, _colour); // 6502: STA VIC+&27 -- sprite 0, the sights'
  }

  void FlightSession::SetSpritesEnabled(std::uint8_t _mask)
  {
    Elite::ApplySpritesEnabled(m_universe.video, _mask); // 6502: STA VIC+&15
  }

  void FlightSession::SetSpriteExpansion(std::uint8_t _mask)
  {
    Elite::ApplySpriteExpansion(m_universe.video, _mask); // 6502: STA VIC+&17 / STA VIC+&1D
  }

  void FlightSession::ShowExplosionSprite(std::uint16_t _x, std::uint8_t _y)
  {
    Elite::ApplyExplosionSprite(m_universe.video, _x, _y); // 6502: the five writes that place sprite 1
  }

  void FlightSession::MaskSprites(std::uint8_t _mask)
  {
    Elite::ApplyMaskSprites(m_universe.video, _mask); // 6502: LDA VIC+&15 / AND #.. / STA VIC+&15
  }

} // namespace Outpost
