#pragma once

#include <cstdint>

#include "Commander.h"
#include "FlightLoop.h"
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
  void Launch(FlightLoop& _loop, StartUpEffects& _start, std::uint8_t& _docked, std::uint8_t _crosshairX, std::uint8_t _crosshairY,
              std::uint8_t _techLevel, SystemSeeds& _selected) noexcept;

} // namespace Elite
