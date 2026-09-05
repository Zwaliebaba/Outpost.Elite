#pragma once

#include <cstdint>

#include "Commander.h"
#include "ExtendedTokens.h"
#include "FlightLoop.h"
#include "PlanetDraw.h"
#include "Spawn.h"
#include "StartUp.h"
#include "Universe.h"

/*
 * Getting into flight, and getting the world ready for it.
 *
 * Four routines the docked half reaches and the flight half assumes: two resets, and the launch
 * that runs one of them. `RES2` was a seam on `StartUpEffects` until this slice -- it was scoped
 * before the stardust, the line heaps and the dashboard existed, and every one of them exists
 * now, so what was left behind the seam was a port waiting to be written (§6.73 a third time).
 */
namespace Elite
{

  /*
   * 6502: ZERO -- `LDX #(de-FRIN)` and a zero-fill down to `FRIN`.
   *
   * FIFTY-NINE BYTES BY ADDRESS, and the port has them in seven structures: the ship slots and
   * counts, the junk tally, the docking computer, both E.C.M. bytes, the mid-jump flag, the cabin
   * temperature, the view laser, the missile arming, the space view, the laser countdown, the gun
   * temperature, the hyperspace effect flag, the explosion count and both message bytes.
   *
   * Written as one list rather than as `memset` on each structure because the ORIGINAL is one
   * range, and what makes it one range is the layout: a routine that cleared "the bubble" and "the
   * flight status" separately would agree with it today and stop agreeing the moment either grew.
   */
  void ClearBubbleState(FlightLoop& _loop) noexcept;

  /*
   * 6502: RES2 -- the ship, the heaps, the dashboard and the stardust, and then straight into ZINF.
   *
   * IT RE-CENTRES THE PITCH AND NOT THE ROLL. `STA JSTY` is there and `STA JSTX` is not, and
   * neither is in `ZERO`'s range -- so a launch leaves the roll rate wherever the last flight left
   * it while the pitch is put back to centre. The ship also starts with `ALPHA`, `ALP1` and `DELTA`
   * all at 3, from one `LDA #3`: a slow roll and a slow drift, which is what a launch looks like.
   */
  void ResetShipAndBubble(FlightLoop& _loop) noexcept;

  /*
   * 6502: RESET -- the universe, and then `RES2`, which it falls into.
   *
   * THE 255 THAT MEANS "DOCKED" IS THE SAME 255 THAT FILLS THE SHIELDS. `LDX #6` counts a loop down
   * past zero, `TXA` takes the 255 it ran off the end with, `STA QQ12` makes that "docked", and the
   * three-byte loop under it fills `FSH`, `ASH` and `ENERGY` with the same byte. One loop counter,
   * two meanings, and the second is only correct because full shields happen to be 255.
   *
   * The seven bytes it zeroes are `BETA` to `BETA+6`, which in THIS build is the pitch pair, both
   * hyperspace counters, `ECMA` and the roll's two sign bytes. The upstream comment lists `XC` and
   * `YC` instead of the last three, which is the BBC's layout -- the third time a documented range
   * has turned out to be another version's (§6.38, §6.45).
   */
  void ResetGame(FlightLoop& _loop, std::uint8_t& _docked) noexcept;

  /// 6502: LDA #12 / STA DELTA -- how fast you leave the slot, and it is four times `RES2`'s 3.
  inline constexpr std::uint8_t LAUNCH_SPEED = 12;

  /*
   * 6502: LDA #8 -- the step `LAUN` hands `HFS2`, and the upstream header comment has it backwards.
   *
   * `HFS2`'s own summary says "4 for launch, 8 for hyperspace"; the instruction inside `LAUN` is
   * `LDA #8`, and the comment beside THAT instruction says 8, "so there are fewer sections in the
   * rings and they are quite polygonal (compared to the step size of 4 used in the much rounder
   * hyperspace rings)". The two are irreconcilable and the code is the one that runs, so the
   * launch tunnel is the polygonal one.
   */
  inline constexpr std::uint8_t LAUNCH_TUNNEL_STEP = 8;

  /*
   * 6502: LAUN, and the `HFS2` it falls into -- the tunnel a launch and an arrival both open with.
   *
   * Three things and then eight rings: the whoosh, the step, and a `TT66` that clears the screen
   * and draws the border box with `QQ11` PUT BACK AFTERWARDS. That last is the whole of why the
   * routine can be run over a docked screen -- `TT66` sets the view to zero as a side effect of
   * clearing it, and `LAUN` saves the byte across the call so the caller's screen type survives an
   * effect drawn on top of it.
   *
   * It was a seam on `StartUpEffects` until this slice, for the reason every other one was: the
   * ball line heap it draws through arrived in 3c and nothing revisited the stub (§6.73, again).
   */
  void DrawLaunchTunnel(FlightScreen& _screen, ClipState& _clip, TunnelEffects* _pacing) noexcept;

  /// 6502: LDA #4 -- the step `LL164` hands `HFS2`, and the rounder of the two. `HFS2`'s header
  /// comment has this pair the wrong way round; see `LAUNCH_TUNNEL_STEP`.
  inline constexpr std::uint8_t HYPERSPACE_TUNNEL_STEP = 4;

  /// 6502: sfxhyp1 -- the hyperspace drive engaging, which `HYPNOISE` plays twice: once pitched
  /// through `NOISE2`, and once more at +128, which is `NOISE`'s "layer it on top" entry.
  inline constexpr std::uint8_t SOUND_HYPERSPACE = 7;

  /// 6502: LDA #&F5 / LDX #240 -- `NOISE2`'s two arguments for the first hyperspace sound. The
  /// low nibble of A is a release length of 5 and the high nibble a sustain volume of 15.
  inline constexpr std::uint8_t HYPERSPACE_SUSTAIN = 0xF5;
  inline constexpr std::uint8_t HYPERSPACE_FREQUENCY = 240;

  /*
   * 6502: HFS2 on its own -- the step, the screen clear, and the eight rings.
   *
   * `LAUN` and `LL164` are the same routine with a different noise and a different step in front
   * of it, which is what `HFS2` taking `A` says: the two entry points differ by two instructions.
   * Splitting it out is what lets the hyperspace tunnel exist without copying the launch's body.
   */
  void DrawTunnel(FlightScreen& _screen, ClipState& _clip, std::uint8_t _step, TunnelEffects* _pacing) noexcept;

  /*
   * 6502: LL164 -- the hyperspace tunnel, and `HYPNOISE` in front of it.
   *
   * Five instructions once `HFS2` exists: the noise, a step of 4, and the rings. `HYPNOISE` is a
   * SOUND routine (the upstream files it as one) and it is played through the seams phase 5 owns,
   * except for its `LDY #1 / JSR DELAY`, which is one vertical sync and is therefore the pacing
   * object's `ShowFrame`.
   *
   * NOTHING IN THE PORT CALLS THIS YET. `MJP` and `TT18` are its only callers and both are 4c, so
   * this is the tunnel waiting for the jump rather than a routine with a live caller -- built here
   * because it is what slice 3d-e names, and because the alternative was to leave `HFS2` reachable
   * at one step size out of two.
   */
  void DrawHyperspaceTunnel(FlightScreen& _screen, ClipState& _clip, DashboardEffects& _sound, TunnelEffects* _pacing) noexcept;

  /*
   * 6502: TT110 -- leave the station, or refuse to.
   *
   * `LDX QQ12 / BEQ NLUNCH` is the refusal: pressing "1" in flight falls straight through to the
   * view change, which is why the key works in both places and does something in only one.
   *
   * The order matters and is not obvious. The tunnel is drawn BEFORE the reset, so it plays over
   * the docked screen; the planet is placed with `INWK+8` at one and the station with it at 128 and
   * `INWK+7` at one, so the two come out of the same zeroed block at different distances; and the
   * contraband fine is ORed into `FIST` on the way out, so leaving is what levies it rather than
   * being scanned.
   */
  void Launch(FlightLoop& _loop, TunnelEffects* _pacing, std::uint8_t& _docked, std::uint8_t _crosshairX, std::uint8_t _crosshairY,
              std::uint8_t _techLevel, SystemSeeds& _selected) noexcept;

  /// 6502: LDA #13 / JSR TT66 / LDA #0 / STA QQ11 -- and it is two values on purpose. `TTX66K`
  /// tail-jumps to `wantdials` for view 0 AND for view 13, so both draw the same pixels; what
  /// differs is that `TT66` prints the view's NAME for a zero, and the title screen has none.
  inline constexpr std::uint8_t TITLE_CLEAR_VIEW = 13;

  /// 6502: LDA #32 / JSR DOVDU19 -- a mode-1 palette command, and the upstream source says in as
  /// many words that it does nothing in this version.
  inline constexpr std::uint8_t TITLE_PALETTE = 32;

  /// 6502: LDA #96 / STA INWK+14 and STA INWK+7 -- the nose vector's z high byte, and the ship's
  /// own z high byte. The second is what `TLL2` walks down, so it is where the ship starts.
  inline constexpr std::uint8_t TITLE_START_DISTANCE = 96;

  /// 6502: LDX #127 / STX INWK+29 / STX INWK+30 -- the maximum roll and pitch counters, which is
  /// the whole of why the ship turns.
  inline constexpr std::uint8_t TITLE_SPIN = 127;

  /// 6502: LDA #12 / STA CNT2 and LDA #5 / STA MCNT -- the two counters the loop is entered with.
  /// 6502: LDA #15 / STA YC / LDA #1 / STA XC -- TITLE's own cursor for the prompt, which
  /// OVERWRITES the column `BR1` set three instructions earlier.
  inline constexpr std::uint8_t TITLE_PROMPT_ROW = 15;
  inline constexpr std::uint8_t TITLE_PROMPT_LEFT = 1;

  inline constexpr std::uint8_t TITLE_CNT2 = 12;
  inline constexpr std::uint8_t TITLE_MCNT = 5;

  /// 6502: the tokens `TITLE` prints before the caller's own -- 30 through `plf`, 13 for the
  /// author names, and 12 for "by D.Braben & I.Bell".
  inline constexpr std::uint8_t TITLE_HEADING_TOKEN = 30;
  inline constexpr std::uint8_t TITLE_AUTHORS_TOKEN = 13;
  inline constexpr std::uint8_t TITLE_BYLINE_TOKEN = 12;

  /*
   * Everything `TITLE` reaches that `FlightLoop` does not already carry.
   *
   * A struct for the fourth time and the same reason as `TradeScreen`, `GameStart` and
   * `FlightScreen`: the alternative is an eight-argument function. Every field is one 6502 label
   * or one seam, and `TITLE` genuinely touches all of it -- it resets the universe, prints through
   * the extended tokeniser, writes a configuration byte and creates a ship.
   */
  struct TitleScreen
  {
    FlightLoop& loop;
    StartUpEffects& effects;
    ExtendedTokenPrinter& tokens; ///< 6502: DETOK, which `TITLE` calls three times
    ControlOptions& options;      ///< 6502: JSTK, which the dismissing key sets, and PATG
    KeyLogger& keys;              ///< 6502: KLO -- `ZEKTRAN` clears it and `BIT KY7` reads it
    std::uint8_t& dockedFlag;     ///< 6502: QQ12, because `RESET` writes it
  };

  /*
   * 6502: TITLE -- the title screen, its rotating ship, and the key that dismisses it.
   *
   * THE KEY YOU DISMISS IT WITH CONFIGURES THE JOYSTICK. `JSTK` is set to &FF immediately before
   * the loop and the exit is `BIT KY7 / BMI TL3 / BCC TLL2 / INC JSTK`: pressing FIRE leaves the
   * &FF and returns, and pressing anything else runs the `INC` first, which makes it zero. So
   * "press space or fire" is not a prompt with two equal answers -- it is the input-device
   * question, asked without saying so.
   *
   * THE SHIP FLIES TOWARDS YOU. `INWK+7` starts at 96 and the loop decrements it to 1, so the ship
   * closes over the first ninety-five frames and then holds; `distaway` is pinned back into
   * `INWK+6` every frame, which is the low byte, so the DISTANCE argument decides where it settles
   * and not how big it starts. The port's placeholder box sized itself off that argument and had
   * the wrong byte.
   *
   * AND `LDA MCNT / AND #3` IS DEAD. The next instruction is `LDA #0`, so the accumulator that
   * computation produced is thrown away before anything reads it. Fourth piece of dead code found
   * in the original, after `cntr`'s `REDU`, `.OLDBOX`'s cursor store and `MAS2`'s second entry.
   *
   * Returns `thiskey` -- the key NUMBER, not the character. `BR1` compares it against `KEY_YES_
   * INTERNAL`, which is 39 and not `'Y'`.
   */
  [[nodiscard]] std::uint8_t ShowTitleShip(TitleScreen& _title, std::uint8_t _token, std::uint8_t _shipType,
                                           std::uint8_t _distance) noexcept;

  /*
   * 6502: what `TT66` is actually called with in `DEATH` -- MEASURED, and it is not 6 (§6.117).
   *
   * The upstream comment says `LDX #24 / JSR DET1` hides the dashboard "and sets A to 6 in the
   * process", which is the BBC's `DET1`: `LDA #6 / SEI / STA VIA+&00 / STX VIA+&01 / CLI`. On this
   * build `DET1` is ONE BYTE, `&60`, a bare `RTS` -- the whole routine is behind an `IF` the C64
   * fails. So neither the `LDX` nor the `LDA` happens, and `TT66` gets whatever `RES2` left in A.
   *
   * That byte is **224**, read off the oracle rather than derived, because it comes out of `RES2`
   * through a dozen instructions that were not written to produce it. `TheDeathScreenSetsUpLikeDEATH`
   * compares it against the shipped routine so it cannot drift.
   */
  inline constexpr std::uint8_t DEATH_VIEW = 224;

  /// 6502: LDA #146 -- the recursive token `DEATH` prints, "{all caps}GAME OVER".
  inline constexpr std::uint8_t GAME_OVER_TOKEN = 146;

  /// 6502: LDA #12 / JSR DOYC / JSR DOXC -- the cursor, moved to the middle of the screen.
  inline constexpr std::uint8_t GAME_OVER_ROW = 12;
  inline constexpr std::uint8_t GAME_OVER_COLUMN = 12;

  /// 6502: SCBASE+&118 -- the second byte `BOX` STORES rather than EORs, so the second that a
  /// redraw cannot rub out. `BOTTOM_RIGHT_CORNER` in `ViewChange.h` is the first.
  inline constexpr std::uint16_t BORDER_TOP_RIGHT = 0x118;

  /// 6502: LDY #64 / STY LASCT -- how long the death animation lasts, in flight-loop iterations.
  inline constexpr std::uint8_t DEATH_FRAMES = 64;

  /// 6502: LDA FRIN+4 / BEQ D1 -- the debris loop fills slots until the FIFTH one is taken.
  inline constexpr std::size_t DEATH_DEBRIS_SLOT = 4;

  /*
   * 6502: DEATH -- the chaos of our destruction, over a "GAME OVER" sign.
   *
   * The sequence is: the sound, `RES2`, a quarter of our speed, a cleared screen with the border
   * EORed off again, a fresh stardust field, the sign, then five pieces of wreckage spawned in
   * random directions and 64 iterations of the whole flight loop to fly them past.
   *
   * `DET1` IS A BARE `RTS` ON THIS BUILD and the port does not call it, which is not a shortcut --
   * see §6.117. The upstream comment says the `LDX #24 / JSR DET1` pair hides the dashboard "and
   * sets A to 6 in the process", and both halves are the BBC's: the C64's `DET1` is one byte.
   *
   * It does not return. The original ends `JMP DEATH2`, which resets the stack and falls into
   * `BR1` -- so this ends where the caller's own death exit already goes.
   */
  /*
   * 6502: DEATH from its start to the `JSR U%` -- the scene, before anything moves.
   *
   * Split from the animation because the routine is two things and not one: everything above `U%`
   * builds a screen and a bubble, and everything below it runs the flight loop over them sixty-four
   * times. The seam is the original's own -- `.D1`'s loop ends and `U%` begins -- and it is what
   * lets the scene be compared against the shipped routine on the whole bitmap, which a routine
   * that never returns cannot be.
   */
  void PrepareDeathScene(FlightLoop& _loop, DashboardEffects& _sound) noexcept;

  void Die(FlightLoop& _loop, DashboardEffects& _sound) noexcept;

} // namespace Elite
