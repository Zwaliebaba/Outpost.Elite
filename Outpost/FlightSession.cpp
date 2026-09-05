#include "pch.h"

#include "FlightSession.h"
#include "SoundOutput.h"

#include "Tactics.h"

#include "DockedKeys.h"

#include "ShipBlueprint.h"

namespace Outpost
{

  namespace
  {
    /*
     * 6502: what `CIRCLE` would have left in `STP`, for a flight world that has never drawn one.
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
     * The seed stays because §6.95's RULE stands -- a default-constructed flight world is still a
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

  FlightSession::FlightSession(Window& _window, Elite::Canvas& _canvas, Elite::TextState& _text, Elite::CharacterPrinter& _characters,
                               Elite::TokenPrinter& _printer, Elite::MessageState& _message, Elite::CommanderBlock& _commander,
                               Elite::Rng& _rng, Elite::FlightStatus& _status, std::uint8_t& _view, std::uint8_t& _explosions,
                               std::uint8_t& _techLevel, Elite::SoundBuffer& _sound, Elite::MusicPlayer& _music, SoundOutput& _audio) noexcept
    : m_window(_window),
      m_canvas(_canvas),
      m_sound(_sound),
      m_music(_music),
      m_audio(_audio),
      m_screen{_canvas,
               m_draw,
               m_math,
               m_geometry,
               m_dust,
               m_heaps,
               m_bubble,
               m_work,
               m_screenState,
               _text,
               _characters.state,
               _printer,
               _characters,
               _message,
               m_flight,
               _status,
               m_compass,
               _rng,
               _commander,
               m_trumbleSprites,
               *this,
               *this,
               _view,
               m_spaceView,
               _explosions,
               _techLevel},
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

    /*
     * 6502: XX21+2*SST-2 -- a third byte of the same shape, and this one is not left by a previous
     * screen at all: `BEGIN` writes it at boot and only `NWSPS` writes it afterwards. Zero is what
     * `NWSHP` refuses, so an unseeded session would silently never build a station.
     */
    m_bubble.stationBlueprint = Elite::BlueprintAddress(Elite::SHIP_TYPE_STATION);

    /*
     * 6502: LSO -- and the station's line heap is IT, not a run carved out of `SLSP` (§6.112).
     *
     * `NWSPS` points the station at the sun's 200 bytes, which live in `m_heaps` here and not in
     * the arena `m_heap` addresses. Lending the window is what makes the station's lines land
     * somewhere; without it every one of them is written out of range and dropped, and the station
     * you have just launched from is invisible in the rear view.
     */
    m_heap.AttachSunHeap(Elite::SUN_HEAP_ADDRESS, m_heaps.sun);
  }

  void FlightSession::SyncVideoRegisters() noexcept
  {
    // 6502: LDA abraxas / STA VIC+&18 and LDA caravanserai / STA VIC+&11, once a frame.
    m_canvas.SetDashboardShown(m_screenState.colourBank == Elite::COLOUR_BANK_DASHBOARD);
  }

  // ---- the sound ----------------------------------------------------------------------------------

  bool FlightSession::PlaySound(std::uint8_t _effect, bool _carryIn)
  {
    /*
     * 6502: NOISE, for real (slice 5a).
     *
     * The seam took its carry argument before there was anything to hand it to (§6.99, §6.118), and
     * this is the day that paid off: the three answers `NOISE` gives are the port's now, and nothing
     * above this line had to change to receive them.
     */
    return Elite::PlaySoundEffect(m_sound, _effect, _carryIn);
  }

  bool FlightSession::PlaySoundPitched(std::uint8_t _effect, std::uint8_t _sustain, std::uint8_t _frequency)
  {
    // 6502: NOISE2. Its callers -- EXNO and EXNO2 -- reach it by JMP and their own callers drop the
    // carry, so the entry carry is not observable here and false is what the seam's contract says.
    return Elite::PlaySoundEffectPitched(m_sound, _effect, _sustain, _frequency, false);
  }

  void FlightSession::StopSound(std::uint8_t _effect)
  {
    Elite::StopSoundEffect(m_sound, _effect); // 6502: NOISEOFF
  }

  void FlightSession::MoveTrumbles()
  {
    // 6502: MVTRIBS -- and it is a CALL written as two jumps (§6.82), so a frame with Trumbles
    // aboard still runs its other fifteen parts. Phase 4 owns the Trumbles themselves.
  }

  void FlightSession::StartDockingMusic()
  {
    // 6502: startbd -- BDENTRY's writes go to the output's direct log, ahead of the next interrupt.
    Elite::StartDockingMusic(m_music, m_audio.Direct());
  }

  void FlightSession::StopDockingMusic()
  {
    // 6502: stopbd -- which reads MULIE, the title screen's bracket around its RESET.
    Elite::StopDockingMusic(m_music, m_screen.status.titleReset, m_sound, m_audio.Direct());
  }

  // ---- the bubble ---------------------------------------------------------------------------------

  /*
   * 6502: FRS1, ANGRY and SFS1 -- three seams answered by slice 4a-b, 2026-09-05.
   *
   * All three were stubs that said "phase 4", and each had a comment explaining which answer an
   * empty implementation must give so that its caller stayed honest. Those answers are gone now,
   * and the routines behind them are compared against the shipped game byte for byte. What is
   * still missing is not the spawning: it is `TACTICS`, so a ship that `Anger` makes hostile has
   * nothing to do about it yet.
   */
  bool FlightSession::SpawnAhead(std::uint8_t _type)
  {
    return Elite::SpawnShipAhead(m_bubble, m_work, _type, m_flight.delta, m_bubble.missileTarget, m_flight.blueprint).created;
  }

  void FlightSession::Anger(std::uint8_t _slot, std::uint8_t _type)
  {
    // 6502: ANGRY on the block INF points at -- and which block that is, the caller says (§6.142).
    Elite::Anger(m_bubble, m_flight, _slot, _type);
  }

  bool FlightSession::SpawnChild(std::uint8_t _aiFlag, std::uint8_t _type)
  {
    // 6502: SFS1 with `INF` at the ship being processed, which is `XSAV`'s slot.
    return Elite::SpawnChildShip(m_bubble, m_work, m_screen.rng, m_math, m_flight.slot, m_flight.type, _aiFlag, _type, m_flight.blueprint)
      .created;
  }

  // ---- the ships ----------------------------------------------------------------------------------

  bool FlightSession::RunTactics(Elite::ShipBlock& _work)
  {
    // 6502: JSR TACTICS from `MVEIT`'s `MV26`, with `INF` at the slot being moved -- which is
    // `XSAV`, the byte the loop keeps for exactly this.
    (void)_work;
    return Elite::RunTactics(m_loop, m_flight.slot);
  }

  void FlightSession::DrawPlanetOrSun()
  {
    // 6502: LL25 -- JMP PLANET, taken for a type with bit 7 set. `INWK` is the body and `TYPE`
    // decides which of the two it is, exactly as the tail jump does.
    Elite::DrawPlanetOrSun(m_canvas, m_heaps, m_draw, m_geometry, m_math, m_clip, m_screen.rng, m_work, m_projection, m_flight.type);
  }

  void FlightSession::DrawExplosion()
  {
    // 6502: LL14's JMP DOEXP -- age the cloud by one frame and draw it, which is how the last
    // frame is erased as well as how this one appears. `INWK` is the exploding ship and `XX3` the
    // vertices `LL9` part 8 projected, which `DOEXP` copies onto the ship's line heap.
    Elite::DrawExplosionCloud(m_canvas, m_draw, m_math, m_screen.rng, m_work, m_heap, m_geometry, m_bubble, *this);
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
    (void)ScanMatrix(m_keys); // 6502: JSR RDKEY, whose answer `DOKEY` does not read
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
    m_rasterMode = RASTER_MODE_SCANNING;                                        // 6502: LDA #%101 / JSR SETL1
    m_spriteMask = static_cast<std::uint8_t>(m_spriteMask & RDKEY_SPRITE_MASK); // 6502: AND #%11111101
    _keys.fill(0u);                                                             // 6502: JSR ZEKTRAN

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
    if (m_screen.view != 0u)
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
    if (Elite::IsChartView(m_screen.view))
    {
      for (const std::size_t index : {Elite::KEY_ROLL_LEFT, Elite::KEY_ROLL_RIGHT, Elite::KEY_PITCH_UP, Elite::KEY_PITCH_DOWN})
      {
        _keys[index] = 0u;
      }
    }

    m_rasterMode = RASTER_MODE_NORMAL; // 6502: LDA #%100 / JSR SETL1
    return answer;
  }

  void FlightSession::DrawRangeCircle(const Elite::RangeCircle& _circle)
  {
    /*
     * 6502: TT128 -- STA K3 / STA K4 / STX K3+1 / STX K4+1 / INX / STX LSP / LDX #2 / STX STP /
     * JMP CIRCLE2.
     *
     * `LSP` goes to ONE rather than to zero: the ball heap's first byte is not a line, so an empty
     * heap is a pointer of 1 and a `LSP` of 0 would make `BLINE`'s first segment overwrite it.
     */
    m_heaps.lsp = 1u;
    m_heaps.stp = _circle.step;
    m_math.k[0] = _circle.radius;

    const Elite::Projection centre{_circle.x, 0u, _circle.y, 0u};
    Elite::DrawBall(m_canvas, m_heaps, m_draw, m_geometry, m_math, m_clip, centre, false);
  }

  void FlightSession::DrawSystemDisc(std::uint8_t _x, std::uint8_t _y, std::uint8_t _radius)
  {
    /*
     * 6502: TT23's ee1 -- JSR FLFLLS / JSR SUN / JSR FLFLLS.
     *
     * The sun is drawn and then FORGOTTEN, twice over: the heap is cleared before so that `SUN`
     * has nothing to erase, and cleared after so that the next disc does not rub this one out. A
     * chart's discs are the one place the game draws suns it never intends to move.
     */
    Elite::ClearSunHeap(m_heaps);

    m_math.k[0] = _radius;
    const Elite::Projection centre{_x, 0u, _y, 0u};
    Elite::DrawSun(m_canvas, m_heaps, m_draw, m_math, m_screen.rng, centre);

    Elite::ClearSunHeap(m_heaps);
  }

  void FlightSession::RunDockingComputer(Elite::ShipBlock& _work)
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
    (void)Elite::RunDockingComputer(m_loop, 0u);
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

  void FlightSession::SetSpriteExpansion(std::uint8_t _mask)
  {
    // 6502: STA VIC+&17 / STA VIC+&1D -- the same byte into both, so a sprite is double size in
    // both directions or in neither.
    m_spriteExpansion = _mask;
  }

  void FlightSession::ShowExplosionSprite(std::uint16_t _x, std::uint8_t _y)
  {
    /*
     * 6502: STX VIC+&2 / STY VIC+&3, the ninth x bit into bit 1 of VIC+&10, and bit 1 of VIC+&15
     * to switch sprite 1 on.
     *
     * The nine-bit x arrives whole and is split here rather than in `GameLogic`, because the split
     * is the register layout's and not the game's -- the original spells it `ORA exlook,X` over a
     * two-byte table whose only job is to shift a 0 or 1 left one place.
     */
    m_burstX = _x;
    m_burstY = _y;
    m_spriteMask = static_cast<std::uint8_t>(m_spriteMask | 0x02u);
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
