#include "pch.h"

#include "FlightSession.h"

#include "ShipBlueprint.h"

namespace Outpost
{

  namespace
  {
    /*
     * 6502: what `CIRCLE` would have left in `STP`, for a flight world that has never drawn one.
     *
     * §6.95: nothing on the path from a cold start to the first launch writes `STP`, and `TT110`
     * calls `HFS1`, which walks a circle `STP` at a time and cannot terminate on a zero. `CIRCLE`
     * is the only writer in the whole build and it stores 8, 4 or 2 by radius. Four is the middle
     * one -- what a planet of ordinary size leaves -- and it is the value the oracle comparison
     * seeds, so the app starts in a state the game could be in rather than in one it could not.
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

  FlightSession::FlightSession(Window& _window, Elite::Canvas& _canvas, Elite::TextState& _text, Elite::CharacterPrinter& _characters,
                               Elite::TokenPrinter& _printer, Elite::MessageState& _message, Elite::CommanderBlock& _commander,
                               Elite::Rng& _rng, Elite::FlightStatus& _status, std::uint8_t& _view, std::uint8_t& _explosions) noexcept
    : m_window(_window),
      m_canvas(_canvas),
      m_screen{
        _canvas,  m_draw,      m_math,     m_geometry, m_dust,  m_heaps,   m_bubble, m_work,     m_screenState,    _text, _characters.state,
        _printer, _characters, _message,   m_flight,   _status, m_compass, _rng,     _commander, m_trumbleSprites, *this, *this,
        _view,    m_spaceView, _explosions},
      m_loop{m_screen, m_keys, m_control, m_options, m_burst, m_heap, m_clip, m_projection, m_axes, *this, *this, *this}
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
    m_heaps.stp = LAST_CIRCLE_STEP;
    m_flight.blueprint = Elite::BlueprintAddress(Elite::SHIP_COBRA_MK3);
  }

  void FlightSession::SyncVideoRegisters() noexcept
  {
    // 6502: LDA abraxas / STA VIC+&18 and LDA caravanserai / STA VIC+&11, once a frame.
    m_canvas.SetDashboardShown(m_screenState.colourBank == Elite::COLOUR_BANK_DASHBOARD);
  }

  // ---- the sound ----------------------------------------------------------------------------------

  bool FlightSession::PlaySound(std::uint8_t _effect)
  {
    /*
     * 6502: NOISE. Phase 5 owns the SID.
     *
     * IT RETURNS THE ANSWER A SOUNDING BUILD GIVES, not the one a silent one does, and the two are
     * different in a way the caller can see. `NOISE` ends `SEC / RTS` when a voice took the effect;
     * with sound switched off (`DNOIZ`) it branches to `SOUR1`, which is a bare `RTS`, so the carry
     * passes through unchanged from the caller. §6.88 is what makes that observable: `EXNO3` tail-
     * calls `NOISE` and `OUCH`'s `DORND` runs on the carry, so which piece of equipment an
     * explosion breaks depends on whether the explosion got a voice.
     *
     * The seam is a `bool` with no carry IN, so it cannot express the pass-through even if this
     * wanted to. Answering "a voice took it" is the answer the game gives whenever sound is on,
     * which is what phase 5 will make true; answering the other way would make a silent build
     * diverge from the oracle the tests compare against.
     */
    (void)_effect;
    return true;
  }

  bool FlightSession::PlaySoundPitched(std::uint8_t _effect, std::uint8_t _sustain, std::uint8_t _frequency)
  {
    // 6502: NOISE2 -- `NOISE` with the sustain and frequency supplied rather than read from the
    // effect's table. Same answer, same reason.
    (void)_effect;
    (void)_sustain;
    (void)_frequency;
    return true;
  }

  void FlightSession::StopSound(std::uint8_t _effect)
  {
    (void)_effect; // 6502: NOISEOFF. Phase 5.
  }

  void FlightSession::MoveTrumbles()
  {
    // 6502: MVTRIBS -- and it is a CALL written as two jumps (§6.82), so a frame with Trumbles
    // aboard still runs its other fifteen parts. Phase 4 owns the Trumbles themselves.
  }

  void FlightSession::StartDockingMusic()
  {
    // 6502: startbd -- a second interrupt handler. Phase 5.
  }

  void FlightSession::StopDockingMusic()
  {
    // 6502: stopbd. Phase 5.
  }

  // ---- the bubble ---------------------------------------------------------------------------------

  bool FlightSession::SpawnAhead(std::uint8_t _type)
  {
    /*
     * 6502: FRS1 -- put a ship right in front of us, and answer with the carry.
     *
     * Phase 4. It answers CLEAR, which is "the bubble was full": `FRMIS` reads that as a jammed
     * missile and says so, which is a refusal the player can see rather than a missile that leaves
     * and never arrives. Answering "it fitted" would spend the missile and spawn nothing.
     */
    (void)_type;
    return false;
  }

  void FlightSession::Anger(std::uint8_t _type)
  {
    (void)_type; // 6502: ANGRY. Phase 4.
  }

  void FlightSession::SpawnStation()
  {
    /*
     * 6502: NWSPS -- put the space station back into the bubble on a launch.
     *
     * Phase 4, and it is why a launch leaves you alone in front of a planet. The fourteen
     * instructions above its fall into `NWSHP` SELF-MODIFY `XX21`, writing the Coriolis or the
     * Dodo's blueprint into the station's slot of the pointer table, and the port's table is
     * read-only -- so this is a decision as well as a routine.
     *
     * `SSPR` stays zero as a consequence, and `SSPR` is what part 9's docking check reads, so
     * `LoopOutcome::Docked` cannot be reached until this exists.
     */
  }

  bool FlightSession::SpawnChild(std::uint8_t _aiFlag, std::uint8_t _type)
  {
    // 6502: SFS1 -- drop a piece of wreckage where a ship just died. Phase 4, and it answers
    // "there was no room", which is what an empty implementation must say: `SPIN` gives up on a
    // clear carry and a set one would have the caller believe a splinter is out there.
    (void)_aiFlag;
    (void)_type;
    return false;
  }

  // ---- the ships ----------------------------------------------------------------------------------

  void FlightSession::RunTactics(Elite::ShipBlock& _work)
  {
    (void)_work; // 6502: TACTICS, all seven parts. Phase 4 -- nothing in the bubble fights back.
  }

  void FlightSession::DrawPlanetOrSun()
  {
    // 6502: LL25 -- JMP PLANET, taken for a type with bit 7 set. `INWK` is the body and `TYPE`
    // decides which of the two it is, exactly as the tail jump does.
    Elite::DrawPlanetOrSun(m_canvas, m_heaps, m_draw, m_geometry, m_math, m_clip, m_screen.rng, m_work, m_projection, m_flight.type);
  }

  void FlightSession::DrawExplosion()
  {
    // 6502: DOEXP -- redraw the cloud, which is how it is erased as well as how it appears. Phase
    // 4, so a ship that explodes leaves the screen instead of blooming.
  }

  void FlightSession::SeedExplosionCloud(Elite::LineHeap& _heap, std::uint16_t _address, std::uint16_t _blueprint)
  {
    /*
     * 6502: the `EE55` block -- six instructions, and they are behind a seam rather than in `LL9`
     * for two reasons: what they write is `DOEXP`'s state, and the `JSR DORND` among them runs on
     * whatever carry `LOIN` last left. The port cannot say what that carry is without reading all
     * thirty-two of `LOIN`'s unrolled copies (§6.91), so seeding a cloud nothing draws would
     * consume generator state the oracle does not.
     */
    (void)_heap;
    (void)_address;
    (void)_blueprint;
  }

  // ---- the controls -------------------------------------------------------------------------------

  void FlightSession::ScanKeyboard()
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
    m_rasterMode = RASTER_MODE_SCANNING;                                        // 6502: LDA #%101 / JSR SETL1
    m_spriteMask = static_cast<std::uint8_t>(m_spriteMask & RDKEY_SPRITE_MASK); // 6502: AND #%11111101
    m_keys.fill(0u);                                                            // 6502: JSR ZEKTRAN

    for (std::uint8_t key = 0; key < static_cast<std::uint8_t>(m_keys.size()); ++key)
    {
      if (m_window.Held(key))
      {
        m_keys[key] = 0xFFu; // 6502: DEC KEYLOOK,X, on a byte that has just been zeroed
      }
    }

    // 6502: LDA QQ11 / BEQ allkeys -- with anything but the space view up, the nine keys that act
    // rather than steer are forgotten. This is the one piece of `RDKEY` that is game logic, and it
    // stays with the scan because it depends on what the scan found.
    if (m_screen.view != 0u)
    {
      for (const std::size_t index : NON_STEERING_KEYS)
      {
        m_keys[index] = 0u;
      }
    }

    m_rasterMode = RASTER_MODE_NORMAL; // 6502: LDA #%100 / JSR SETL1
  }

  void FlightSession::RunDockingComputer(Elite::ShipBlock& _work)
  {
    /*
     * 6502: DOCKIT -- phase 4's autopilot. It steers by writing `INWK+27` to `INWK+30`, which
     * `DOKEY` then turns into synthetic key presses; leaving the block alone is therefore "the
     * autopilot asked for nothing", and the ship flies straight rather than erratically.
     */
    (void)_work;
  }

  // ---- the VIC-II ----------------------------------------------------------------------------------

  void FlightSession::SetRasterMode(std::uint8_t _mode)
  {
    // 6502: SETL1 -- self-modifying code inside the interrupt handler (§6.59). The port has no
    // interrupt to modify, so the mode is remembered and nothing reads it yet.
    m_rasterMode = _mode;
  }

  void FlightSession::SetSightColour(std::uint8_t _colour)
  {
    m_spriteColour = _colour; // 6502: STA VIC+&27 -- sprite 0, which is the sights'
  }

  void FlightSession::SetSpritesEnabled(std::uint8_t _mask)
  {
    m_spriteMask = _mask; // 6502: STA VIC+&15
  }

  void FlightSession::MaskSprites(std::uint8_t _mask)
  {
    // 6502: LDA VIC+&15 / AND #.. / STA VIC+&15 -- part 15's read-modify-write, which exists
    // because the game does not know how many Trumble sprites are showing.
    m_spriteMask = static_cast<std::uint8_t>(m_spriteMask & _mask);
  }

  void FlightSession::SetPalette(std::uint8_t _colour)
  {
    // 6502: DOVDU19 -- a mode-1 palette command for the BBC's I/O processor, and the upstream
    // source says in as many words that it does nothing in this version. `LOOK1` calls it with
    // A = 0 before it has even looked at the view, so the port keeps the call and not an effect.
    (void)_colour;
  }

} // namespace Outpost
