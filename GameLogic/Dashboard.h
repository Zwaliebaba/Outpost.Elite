#pragma once

#include <cstdint>

#include "Canvas.h"
#include "Scanner.h"
#include "SoundEffects.h"
#include "ShipDraw.h"
#include "ShipMove.h"
#include "ShipSlot.h"

namespace Elite
{

  struct Universe; // Universe.h -- forward, because Universe.h includes this one


  /*
   * The dashboard (slice 3d-b).
   *
   * Seven bars, two indicators, three bulbs and the compass, redrawn from scratch on every pass of
   * the flight loop -- which is why none of it is EORed the way the scanner and the space view are:
   * `DIL` and `DIL2` STORE their bytes rather than EOR them, so the previous frame is overwritten
   * rather than rubbed out. The bulbs are the exception and they are not bitmap at all.
   */

  /// 6502: RED, YELLOW -- four multicolour pixels of one colour each, which is what `COL` is ANDed
  /// with. The dials use only these two; the scanner's own colours are in `SCANNER_COLOUR_TABLE`.
  inline constexpr std::uint8_t DIAL_DANGER = 0x55; ///< 6502: RED
  inline constexpr std::uint8_t DIAL_NORMAL = 0xAA; ///< 6502: YELLOW

  /*
   * 6502: BULBCOL -- what the three indicator bulbs EOR into SCREEN RAM.
   *
   * Not a bitmap colour and not a mask: it is a palette byte for two character cells, and the bulbs
   * are toggled by EORing it in and out. That is why `ECBLB` and `SPBLB` are four instructions each
   * and why calling either twice puts the screen back.
   */
  inline constexpr std::uint8_t BULB_COLOUR = 0xE0;

  /*
   * 6502: DLOC%, ECELL, SCELL, MCELL -- where the dashboard is.
   *
   * `DLOC%` is `SCBASE + 18*8*40`: the dashboard starts at character row 18, which is the same 144
   * that makes the space view 256 x 144. The three cell addresses are in the SECOND block of screen
   * RAM (`SCBASE + 0x2400`), the one `wantdials` points the VIC-II at, so they are offsets into the
   * canvas's dashboard cells rather than into the bitmap.
   *
   * AND `DLOC%` HAS NO LEFT MARGIN. `ylookup` adds 32 to every row, four character cells of it, and
   * `Canvas::RowOffset` reproduces that -- but `DLOC%` is written out from `SCBASE` directly, so the
   * dashboard starts four cells further left than a row of the space view does. The port added the
   * margin here at first and the first `DIALS` comparison put every dial 32 bytes to the right.
   */
  inline constexpr std::uint16_t DASHBOARD_BITMAP = 18u * 8u * 40u;
  inline constexpr std::uint16_t ECM_CELL = Canvas::DASHBOARD_CELLS + 23u * 40u + 11u;
  inline constexpr std::uint16_t STATION_CELL = Canvas::DASHBOARD_CELLS + 23u * 40u + 28u;
  inline constexpr std::uint16_t MISSILE_CELL = Canvas::DASHBOARD_CELLS + 24u * 40u + 6u;

  /*
   * 6502: GNTMP, QQ22+1, FSH, ASH, ENERGY, CABTMP, ALTIT, ECMA and FLH -- what the dials read.
   *
   * This was `DockedShip` in slice 2d, named for `DOENTRY`, which RESETS six of these rather than
   * for the seven routines that read them. It also held `DELTA`, which is `FlightState`'s: one 6502
   * byte in two C++ fields, §6.28's shape, shipped since 2d and removed here (§6.64).
   *
   * `CABTMP`, `ALTIT` and `FLH` have no writer in this slice -- flight loop part 15 sets the first
   * two and the damage flash is 3d-d's -- so 3d-b reads them and 3d-d fills them in.
   *
   * THREE OF THEM ARE NOT DIALS. `ECMP` joined in 3d-d-i -- it is `ECMA`'s other half, whether the
   * E.C.M. running is ours or somebody else's, and `ECMOF` clears the pair with a single `LDA #0`,
   * which is the shape a struct field and a reference parameter between them would have hidden.
   * `LAS2` and `MJ` joined in 3d-d-iii-a because `TTX66` clears the first and `WARP` reads the
   * second, and neither had anywhere else to be: both are one byte of per-flight state with a
   * single writer and a single reader, which is what everything else here is.
   *
   * So the struct is now "the per-flight bytes" rather than "what the dials read", and the name it
   * kept from 3d-b understates it.
   */
  struct FlightStatus
  {
    std::uint8_t laserTemperature = 0; ///< 6502: GNTMP

    /*
     * 6502: MULIE -- "this `RESET` came from the title screen", and nothing in `GameLogic` reads it.
     *
     * `TITLE` sets it, calls `RESET`, and clears it again; the only reader in the whole build is
     * `stopbd`, whose first two instructions are `BIT MULIE / BMI itsoff` -- so the guard exists to
     * stop the title screen's reset from silencing the music it has just started. `stopbd` is
     * phase 5's, which leaves this a byte the port carries and does not act on, the same shape as
     * `ScreenState::hyperspaceEffect`.
     *
     * It is here rather than nowhere because §6.101 was a store the port dropped for exactly the
     * reason it could have been dropped here: nothing that exists yet reads it.
     */
    std::uint8_t titleReset = 0;
    /*
     * 6502: QQ22 and QQ22+1 -- the hyperspace countdown, which is TWO bytes and one number.
     *
     * `QQ22+1` is what is printed and counts 15 down to 1; `QQ22` is the tick within each of
     * those, reloaded from 5 every time the printed digit changes. The port had only the printed
     * half because that is all `ee3` and `TT66` read; `RESET` clears both, which is what made the
     * other one necessary.
     */
    std::uint8_t hyperspaceCounter = 0;   ///< 6502: QQ22
    std::uint8_t hyperspaceCountdown = 0; ///< 6502: QQ22+1
    std::uint8_t forwardShield = 0;       ///< 6502: FSH
    std::uint8_t aftShield = 0;           ///< 6502: ASH
    std::uint8_t energy = 0;              ///< 6502: ENERGY
    std::uint8_t cabinTemperature = 0;    ///< 6502: CABTMP
    std::uint8_t altitude = 0;            ///< 6502: ALTIT
    std::uint8_t ecmCountdown = 0;        ///< 6502: ECMA
    std::uint8_t ecmOurs = 0;             ///< 6502: ECMP
    std::uint8_t damageFlash = 0;         ///< 6502: FLH

    /// 6502: LAS2 -- the laser power for the view being shown, or zero for "no laser here, stop
    /// pulsing". `TTX66` clears it on every screen change and flight loop part 16 reads it.
    std::uint8_t viewLaser = 0;

    /// 6502: MJ -- non-zero in witchspace. `WARP` refuses to work while it is set, which is why
    /// you cannot skip past a Thargoid ambush.
    std::uint8_t midJump = 0;

    /*
     * 6502: LAS, LASCT and MSAR -- what the guns and the missile are doing this frame.
     *
     * `LAS` is the power of the shot being fired right now and is cleared at the top of every pass;
     * `LASCT` is the pulse laser's countdown, which is why a pulse laser cannot be held down; `MSAR`
     * says the missile is armed and looking for a lock. All three arrived with the flight loop in
     * 3d-d-iii-b for the same reason `LAS2` and `MJ` did: one byte, one writer, one reader.
     */
    std::uint8_t laserPower = 0;   ///< 6502: LAS
    std::uint8_t laserCount = 0;   ///< 6502: LASCT
    std::uint8_t missileArmed = 0; ///< 6502: MSAR
  };

  /// What `PZW` hands back: the original returns one colour in A and another in X, and both of its
  /// callers store both -- `DIALS` part 1 as (K, K+1) and part 3 as (K+1, K), the other way round.
  struct DangerColours
  {
    std::uint8_t a = 0;
    std::uint8_t x = 0;
  };

  /*
   * 6502: PZW -- the colour a dial in the danger zone is drawn in, which flashes.
   *
   * X is always yellow. A is red unless `MCNT AND 8 AND FLH` is non-zero, in which case it is
   * yellow too -- so with the damage flash on, the danger colour alternates every eight passes of
   * the main loop, and with it off the dial is steady red.
   *
   * ITS SECOND PATH IS SPELLED AS DATA. `BEQ P%+4 / TXA / EQUB &2C / LDA #RED / RTS`: the `&2C` is
   * `BIT abs`, whose two operand bytes ARE the `LDA #RED` that follows, so falling into it skips
   * the load and branching past it performs it. One instruction hidden in an addressing mode, and
   * a port written from a disassembly would emit a read of `$55A9` and be right by accident
   * (§6.63).
   */
  [[nodiscard]] DangerColours DangerColour(std::uint8_t _mainLoopCounter, std::uint8_t _damageFlash) noexcept;

  /*
   * 6502: DILX and DIL -- one bar, sixteen steps wide, and FOUR ENTRY POINTS.
   *
   * The routine opens with four `LSR A` and then `.DIL`, so where a caller jumps in IS the scale:
   * `DILX` divides the reading by sixteen, `DILX+2` by four, `DIL-1` by two, and `DIL` not at all.
   * All four are used, so this takes the shift count rather than pretending there is one routine
   * (§6.63) -- and `DIL-1`, the one that reads like a typo, is the speed bar.
   *
   * `_threshold` is `T1`: below it the bar is drawn in `K+1` and at or above it in `K`, except that
   * a `K+1` of zero falls through to `K` as well. `SC` comes in pointing at the bar's first
   * character cell and goes out pointing at the next row down, which is how four calls in a row
   * draw four dials. `Q`, `R` and `COL` are the routine's own (M2-c).
   */
  /// 6502: K and K+1 -- the two colours `DIALS` stores for `DIL`, as `PZW`'s (A X) or the other way
  /// round, which is why the same threshold means the opposite thing for the energy bars.
  struct DialColours
  {
    std::uint8_t atOrAbove = 0; ///< 6502: K -- drawn when the reading reaches `T1`, or when `K+1` is zero
    std::uint8_t below = 0;     ///< 6502: K+1 -- drawn under `T1`, unless it is zero
  };

  void DrawBar(Canvas& _canvas, DrawWorkspace& _draw, std::uint8_t _value, int _shifts, std::uint8_t _threshold,
               DialColours _colours) noexcept;

  /*
   * 6502: DIL2 -- the roll and pitch indicators, which are one lit pixel rather than a bar.
   *
   * Four cells of four pixels, and the pixel is `CTWOS,X` for whichever block the value lands in;
   * every other block is blank. Once the lit block is drawn, `Q` is set to 255 so no later block
   * can match, which is a loop exit spelled as data.
   *
   * The `ADC #&3F` at the end has no `CLC` and does not need one: the only way out of the loop is a
   * `CPY #30` that did not branch, so the carry is set and the add is 320 rather than 64.
   */
  void DrawIndicator(Canvas& _canvas, DrawWorkspace& _draw, std::uint8_t _value) noexcept;

  /*
   * 6502: DIALS parts 1 to 4 -- the whole dashboard, and it ends `JMP COMPAS`.
   *
   * One fall-through chain of four files, so the compass is not something the caller does next: it
   * is the last thing `DIALS` does. Part 3 is the energy bars and runs on ONE PASS IN FOUR
   * (`LDA MCNT / AND #3 / BNE dec27`, and `dec27` is `TT26`'s own `RTS` borrowed as a branch
   * target), so three passes in four draw the other three parts and the compass alone.
   *
   * Part 3's four energy bars are dealt into `XX12`, the same four zero-page bytes `LL51` writes
   * its dot products into. The two are never live at once -- the ships are drawn before the
   * dashboard is -- and part 3 clears all four before reading any, so since M2-c they are this
   * routine's own array rather than a `GeometryWorkspace` borrowed for the name. `_draw` is `SC`,
   * the cursor the dials advance between them.
   */
  void DrawDials(Canvas& _canvas, DrawWorkspace& _draw, const FlightState& _flight, const FlightStatus& _status, std::uint8_t _fuel,
                 Compass& _compass, const Bubble& _bubble) noexcept;

  /*
   * 6502: MSBAR -- set missile indicator X to the colour in Y.
   *
   * A colour cell and not a drawing: the missiles are four character blocks in the second block of
   * screen RAM and this writes one palette byte. `DEX / TXA / INX / EOR #3` turns missile 1 to 4
   * into cell 3 down to 0, so they fill from the right.
   *
   * It leaves Y at zero, which the original's callers rely on and which nothing here does.
   */
  void SetMissileIndicator(Canvas& _canvas, std::uint8_t _missile, std::uint8_t _colour) noexcept;

  /*
   * 6502: msblob -- redraw all four indicators from `NOMSL`.
   *
   * TWO LOOPS AND ONE COUNTER. `.ss` walks X down from four drawing black until it MEETS `NOMSL`,
   * then falls into `.SAL8`, which carries on down the same X drawing green -- so the split point
   * is the count and neither loop knows how many it will draw. `CPX NOMSL / BEQ SAL8` never fires
   * for a full rail, because X starts at four and a rail holds four, so the black loop is skipped
   * entirely; and it never fires for an empty one either, because X reaches zero first.
   */
  void ResetMissileIndicators(Canvas& _canvas, std::uint8_t _missiles) noexcept;

  /*
   * 6502: BLACK2, RED2, YELLOW2, GREEN2 -- the missile indicator's four states, as SCREEN RAM
   * palette bytes rather than bitmap colours.
   *
   * `MISSILE_NONE` WAS ZERO HERE UNTIL 3d-d-iii-b, and zero is not a colour the game ever passes:
   * `BLACK2` is &B7, and `msblob` uses it for every indicator above `NOMSL` while `FRMIS` uses it
   * for the missile that has just left. Nothing was wrong downstream -- `MSBAR` writes whatever
   * byte it is given and the port matched the game on that byte -- but the NAME claimed to be a
   * value the game uses and was not one.
   */
  inline constexpr std::uint8_t MISSILE_NONE = 0xB7;   ///< 6502: BLACK2 -- no missile in this slot
  inline constexpr std::uint8_t MISSILE_LOCKED = 0x27; ///< 6502: RED2 -- armed and locked
  inline constexpr std::uint8_t MISSILE_ARMED = 0x87;  ///< 6502: YELLOW2 -- armed, seeking
  inline constexpr std::uint8_t MISSILE_READY = 0x57;  ///< 6502: GREEN2 -- unarmed

  /*
   * 6502: ABORT2 -- point the leftmost missile at slot X, and recolour its indicator.
   *
   * `STY MSAR` STORES ZERO, not the colour it was handed: `MSBAR` ends `LDY #0`, and the store
   * three instructions later reads that rather than the Y the caller passed. So every call clears
   * "the missile is seeking a lock" whatever colour it sets the light to -- which is a register
   * side effect surviving a `JSR`, and the reason `SetMissileIndicator` is documented as leaving
   * Y at zero even though nothing in the port needs it to (§6.68).
   */
  void SetMissileTarget(Universe& _universe, std::uint8_t _missiles, std::uint8_t _target, std::uint8_t _colour) noexcept;

  /// 6502: ABORT -- `LDX #&FF` and then straight into `ABORT2`: no target, so the lock is off.
  void AbortMissileLock(Universe& _universe, std::uint8_t _missiles, std::uint8_t _colour) noexcept;

  /// 6502: ECBLB -- toggle the E.C.M. bulb, two cells of it, by EORing `BULBCOL` in and out.
  void ToggleEcmIndicator(Canvas& _canvas) noexcept;

  /// 6502: SPBLB -- the same for the space station bulb, seventeen cells to the right.
  void ToggleStationIndicator(Canvas& _canvas) noexcept;

  /// 6502: GREEN2 -- the palette byte `KILLSHP` hands `ABORT`. It was `SpawnEffects::MISSILE_GREEN`
  /// until M3-b-1 took the seam away; the constant belongs beside the routine that takes it.
  inline constexpr std::uint8_t MISSILE_GREEN = 0x57;

  /// What `ECBLB2` and `ECMOF` reach outside this slice: the sound, which is hardware.
  /*
   * `DashboardEffects` WAS HERE AND IS NOT ANY MORE (M3-b-2a).
   *
   * `PlaySound` was `NOISE`, `PlaySoundPitched` was `NOISE2` and `StopSound` was `NOISEOFF` --
   * three routines `SoundEffects.cpp` has had since slice 5a. The seam existed because the SID is
   * written from a RASTER INTERRUPT rather than from the game, and the port had nowhere to keep
   * the buffer between them while sound was phase 5's.
   *
   * IT IS MEMORY, NOT A PORT, and that is why the removal is a `Universe` member rather than a new
   * interface. `NOISE` and its relatives put an effect into `sound_variables` -- ten arrays of
   * three and one byte on its own -- and set a flag; nothing in the game reads the chip back.
   * `SOINT` is the only thing that touches the SID, it runs once a frame from `COMIRQ1`, and the
   * platform is what calls it. So `Universe::sound` is the buffer and the port is the TICK.
   *
   * `PlaySound`'s carry survives the seam and is the reason it answered a `bool`: `NOISE` ends
   * `SEC / RTS` on the path that gives the effect a voice and `CLC / RTS` on the path that refuses,
   * and `LASLI`'s opening `DORND` rolls that flag into its own answer (§6.86, §6.99).
   * `PlaySoundEffect` answers the same `bool` for the same reason.
   */

  /// 6502: sfxecm -- the effect number `ECBLB2` asks for.
  inline constexpr std::uint8_t SOUND_ECM = 9;

  /*
   * 6502: ECBLB2 -- start the E.C.M.: thirty-two passes on the countdown, the noise, and the bulb.
   *
   * It has no `RTS`; it falls into `ECBLB`, so lighting the bulb is part of starting the E.C.M.
   * rather than something the caller does afterwards.
   */
  /// `_carryIn` because `ECBLB2` touches no flag on its way to `NOISE`, so what the sound sees
  /// is what this routine was called with (§6.118).
  void StartEcm(Canvas& _canvas, FlightStatus& _status, SoundBuffer& _sound, bool _carryIn) noexcept;

  /*
   * 6502: ECMOF -- stop the E.C.M.: clear both flags, put the bulb out, silence the hum.
   *
   * The counterpart to `StartEcm` and not its mirror image. Starting it sets `ECMA` alone and
   * leaves `ECMP` to the caller; stopping it clears both. And `ECBLB` is a TOGGLE, so this puts
   * the bulb out only because the bulb was lit -- called with the E.C.M. already off it lights it.
   * The game never does that: `RES2` guards its call with `LDA ECMA / BEQ yu`, and flight loop
   * part 16 only reaches it once the countdown has run down or the energy has run out.
   *
   * The byte before it is an `RTS` belonging to the routine above, which `NO3` and `SFRMIS` both
   * branch to as a cheap return -- `BNE ECMOF-1`. Nothing to port, but it means `ECMOF` cannot be
   * moved without breaking two routines that never mention it.
   */
  void StopEcm(Canvas& _canvas, FlightStatus& _status, SoundBuffer& _sound) noexcept;

} // namespace Elite
