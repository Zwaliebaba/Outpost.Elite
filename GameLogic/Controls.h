#pragma once

#include <array>
#include <cstdint>

#include "PlanetDraw.h"
#include "ShipMove.h"
#include "ShipSlot.h"

namespace Elite
{

/*
 * The player's controls (slice 3d-d-ii).
 *
 * `DOKEY` reads the keyboard and the joystick, and then does 77 instructions of arithmetic on
 * what it read. The reading is hardware and stays behind a seam; the arithmetic is not, and it is
 * as comparable as anything else in this port. `Source-Inventory.md` row 145 files the whole of
 * it under "Replace", which is right about the scan and wrong about the rest (§6.73).
 */

/*
 * 6502: DAMP, DJD and JSTK -- three of the configuration bytes the title screen toggles.
 *
 * `DKS3` toggles a byte between 0 and &FF with `EOR #&FF`, and indexes the block as `DAMP-&40,X`
 * from the key code, which is why they are adjacent in memory and in that order. TWO OF THE THREE
 * READ BACKWARDS: `DAMP` non-zero means damping is OFF and `DJD` non-zero means auto-recentre is
 * OFF, because the options are phrased as the thing being disabled. `JSTK` is the plain way round.
 *
 * The block holds three more the port keeps elsewhere -- `DNOIZ` is the sound's, `FLH` is
 * `FlightStatus`'s damage flash, and `JSTGY` inverts the joystick's Y axis, which on the C64 is
 * read inside `RDKEY` and so never reaches `GameLogic`.
 */
struct ControlOptions
{
  std::uint8_t dampingDisabled = 0;  ///< 6502: DAMP
  std::uint8_t recentreDisabled = 0; ///< 6502: DJD
  std::uint8_t joystick = 0;         ///< 6502: JSTK
};

/*
 * 6502: JSTX, JSTY and `auto` -- what the player is asking the ship to do.
 *
 * Not in the configuration block: these are six kilobytes away in the UP workspace, and they do
 * not survive a new commander. Both rates run 1 to 255 with 128 as centred, so 1 is full left or
 * full up and 255 is full right or full down.
 */
struct ControlState
{
  std::uint8_t roll = 128;          ///< 6502: JSTX
  std::uint8_t pitch = 128;         ///< 6502: JSTY
  std::uint8_t dockingComputer = 0; ///< 6502: auto
};

/*
 * 6502: BUMP2 -- add `_amount` to a control rate, clamping at 255 and re-centring on the way.
 *
 * `REDU2` is its mirror and the two are ONE routine spread over two files that call into each
 * other: `BUMP2` ends `BPL djd1`, and `djd1` is inside `REDU2`; `REDU2` ends `BPL RE2+2`, and
 * `RE2` is inside `BUMP2`. `RE2+2` is also a MID-INSTRUCTION address -- `RE2` is `BPL djd1`, two
 * bytes, so `RE2+2` is the `LDA T / RTS` after it. Three entries between two routines, and none
 * of them is a label a caller uses.
 *
 * The re-centring is the point. Bumping a rate that lands in the LEFT half of the slider means
 * the player is pushing back through the middle, so with auto-recentre configured the rate jumps
 * straight to 128 rather than crawling there. `_recentreDisabled` is `DJD`, and non-zero means
 * the jump does not happen.
 *
 * A returns unchanged -- the routine saves it in `T` and restores it -- so there is nothing for
 * the port to hand back but the rate.
 */
[[nodiscard]] std::uint8_t BumpControl(std::uint8_t _value, std::uint8_t _amount,
                                       std::uint8_t _recentreDisabled) noexcept;

/*
 * 6502: REDU2 -- subtract `_amount` from a control rate, clamping at 1 and re-centring.
 *
 * AND THE CLAMP HAS A HOLE. `SBC` leaves the carry SET when it did not borrow, so `BCS RE3`
 * skips the `LDX #1` whenever the value was greater than or equal to the amount -- including
 * when they are equal, which produces ZERO. The routine's own documentation says the rate runs
 * from 1 to 255. `DOKEY` calls it with `_amount` = 14, so a rate of exactly 14 becomes 0, and the
 * next pass of `cntr` bumps it back to 1. Reachable, harmless, and not what the comment says.
 */
[[nodiscard]] std::uint8_t ReduceControl(std::uint8_t _value, std::uint8_t _amount,
                                         std::uint8_t _recentreDisabled) noexcept;

/*
 * 6502: KEYLOOK, which `KLO` is another name for -- sixty-five bytes, one per key the game
 * watches, indexed by the C64's internal key number rather than by anything meaningful.
 *
 * The scan sets a byte while its key is held and `ZEKTRAN` zeroes the lot. `DOKEY` both reads it
 * and WRITES it: with the docking computer flying, the autopilot presses the keys itself, which
 * is why this is not an input parameter.
 */
using KeyLogger = std::array<std::uint8_t, 65>;

/// 6502: KY1 to KY7 -- offsets of the flight keys within `KLO`, which are their key numbers.
inline constexpr std::size_t KEY_SLOW_DOWN = 9;   ///< 6502: KY1 -- "?"
inline constexpr std::size_t KEY_SPEED_UP = 4;    ///< 6502: KY2 -- Space
inline constexpr std::size_t KEY_ROLL_LEFT = 17;  ///< 6502: KY3 -- "<"
inline constexpr std::size_t KEY_ROLL_RIGHT = 20; ///< 6502: KY4 -- ">"
inline constexpr std::size_t KEY_PITCH_UP = 41;   ///< 6502: KY5 -- "X"
inline constexpr std::size_t KEY_PITCH_DOWN = 51; ///< 6502: KY6 -- "S"
inline constexpr std::size_t KEY_FIRE = 54;       ///< 6502: KY7 -- "A"

/// 6502: LDA #14 -- what `DOKEY` bumps and reduces the rates by on every pass.
inline constexpr std::uint8_t CONTROL_STEP = 14;

/// 6502: the two things `DOKEY`'s flight half reaches that are not memory.
class ControlEffects
{
public:
  virtual ~ControlEffects() = default;

  /*
   * 6502: JSR RDKEY -- the CIA keyboard-matrix scan and the joystick port.
   *
   * Hardware from end to end: it walks `&DC00`/`&DC01` eight columns at a time, reads the
   * joystick from `CIA`, and brackets the whole thing in `SETL1` calls that switch the raster
   * interrupt. It rewrites the key logger as its output. One piece of it IS game logic -- the
   * tail clears `KY12` to `KY20` when `QQ11` says this is not a space view -- but that depends
   * on what the scan itself found, so it stays with the scan.
   */
  virtual void ScanKeyboard() = 0;

  /// 6502: JSR DOCKIT -- phase 4's docking autopilot. It reads the ship block and writes
  /// `INWK+27` to `INWK+30`, which is how it steers: as an acceleration and three rates.
  virtual void RunDockingComputer(ShipBlock& _work) = 0;
};

/*
 * 6502: DOKEY's flight half -- turn what is held down into a roll rate and a pitch rate.
 *
 * TWO ROUTINES IN ONE, and the ledger files both under the keyboard scan (§6.73). The scan is
 * `RDKEY` and is hardware. What is left is arithmetic on `JSTX` and `JSTY` that touches no
 * register at all.
 *
 * With `auto` set the docking computer flies instead, and it does it by PRESSING KEYS: `DOCKIT`
 * leaves an acceleration in `INWK+28` and rates in `INWK+29` and `INWK+30`, and this turns each
 * of them into a synthetic entry in the key logger, so everything downstream cannot tell the
 * autopilot from the player. The exception is a large roll request, where it writes `JSTX`
 * directly AND clears the key it would otherwise have pressed.
 *
 * `ASL INWK+29` is doing three jobs at once: it doubles the request, its carry out is the old
 * sign (which picks the key), and the `BIT` after it reads the NEW sign (which decides whether
 * the request is large enough to bypass the keys). Reading it as a shift alone gets the direction
 * right and the magnitude wrong.
 *
 * The routine ends by falling into `DK4`, the docked dispatcher, which is not this unit's.
 */
void ReadFlightControls(KeyLogger& _keys, ControlState& _control, const ControlOptions& _options,
                        ShipBlock& _work, FlightState& _flight, ControlEffects& _effects) noexcept;

} // namespace Elite
