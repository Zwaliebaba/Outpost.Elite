#pragma once

#include <array>
#include <cstdint>

#include "Canvas.h"
#include "Commander.h"
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
   * 6502: DAMP, DJD and JSTK -- three of the configuration bytes the PAUSE screen toggles.
   *
   * `DKS3` toggles a byte between 0 and &FF with `EOR #&FF`. On this build it walks the block as
   * `DAMP,Y` and compares the key against `TGINT,Y`, a table of key codes in block order -- the
   * `DAMP-&40,X` of the BBC, indexed straight from the key code, is what an earlier version of this
   * comment described, and it is not here. The screen that does the walking is `DK4`, which `DOKEY`
   * falls into every frame, and it is not ported yet (slice 4e, §6.120). TWO OF THE THREE READ
   * BACKWARDS: `DAMP` non-zero means damping is OFF and `DJD` non-zero means auto-recentre is OFF,
   * because the options are phrased as the thing being disabled. `JSTK` is the plain way round.
   *
   * The block holds ten more the port keeps elsewhere or not yet -- `DNOIZ` is the sound's, `FLH`
   * is `FlightStatus`'s damage flash, `JSTGY` inverts the joystick's Y axis (read inside `RDKEY`,
   * so it never reaches `GameLogic`), `PLTOG` is `PlanetSunState`'s, and the music's five wait for
   * phase 5.
   */
  struct ControlOptions
  {
    std::uint8_t dampingDisabled = 0;  ///< 6502: DAMP
    std::uint8_t recentreDisabled = 0; ///< 6502: DJD
    std::uint8_t joystick = 0;         ///< 6502: JSTK

    /*
     * 6502: PATG -- "show the author names on the title screen", and it does two things.
     *
     * `TITLE` prints an extra token when it is set, which is the visible half. The other half is
     * in the main game loop's spawning: five of its tests are `AND PATG`, so switching the credits
     * on also changes what the universe puts in front of you. One configuration byte, two
     * unrelated effects, and only one of them is what its name says.
     *
     * It is in this block because the block is what it is: `DKS3` toggles `DAMP,Y` for the key
     * `TGINT,Y` names, so the block's order is the key table's order and nothing else.
     */
    std::uint8_t authorNames = 0;
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
  [[nodiscard]] std::uint8_t BumpControl(std::uint8_t _value, std::uint8_t _amount, std::uint8_t _recentreDisabled) noexcept;

  /*
   * 6502: REDU2 -- subtract `_amount` from a control rate, clamping at 1 and re-centring.
   *
   * AND THE CLAMP HAS A HOLE. `SBC` leaves the carry SET when it did not borrow, so `BCS RE3`
   * skips the `LDX #1` whenever the value was greater than or equal to the amount -- including
   * when they are equal, which produces ZERO. The routine's own documentation says the rate runs
   * from 1 to 255. `DOKEY` calls it with `_amount` = 14, so a rate of exactly 14 becomes 0, and the
   * next pass of `cntr` bumps it back to 1. Reachable, harmless, and not what the comment says.
   */
  [[nodiscard]] std::uint8_t ReduceControl(std::uint8_t _value, std::uint8_t _amount, std::uint8_t _recentreDisabled) noexcept;

  /*
   * 6502: KEYLOOK, which `KLO` is another name for -- sixty-five bytes, one per key the game
   * watches, indexed by the C64's internal key number rather than by anything meaningful.
   *
   * The scan sets a byte while its key is held and `ZEKTRAN` zeroes the lot. `DOKEY` both reads it
   * and WRITES it: with the docking computer flying, the autopilot presses the keys itself, which
   * is why this is not an input parameter.
   */
  using KeyLogger = std::array<std::uint8_t, 65>;

  /*
   * 6502: U% -- clear the flight keys, which is NOT `ZEKTRAN` however much it looks like one.
   *
   * `LDA #0 / LDY #56 / .DKL3 STA KLO,Y / DEY / BNE DKL3 / STA KL`. It walks DOWN to one, so it
   * clears `KLO+1` to `KLO+56` -- fifty-six bytes of sixty-five, and `ZEKTRAN` clears the lot.
   * The nine it leaves alone are `KLO+0` and the eight above `KY20`. `DEATH` is the only caller:
   * the keys are wiped before the death sequence runs the flight loop, so nothing the player was
   * holding when they died steers the wreckage.
   *
   * THE `STA KL` AFTER THE LOOP IS NOT `KLO+0`. On the BBC `KL` and `KLO` are the same table, so
   * the store is the loop's missing zeroth byte; on the C64 `KL` is a separate byte at &441 that
   * `DK4` writes `thiskey` into and NOTHING reads, so the store clears dead memory and the port
   * has no byte to clear for it (§6.117). The first version of this routine cleared `KLO+0` for
   * it, which the shipped game does not.
   */
  /// 6502: LDY #56 -- the highest index `U%` clears; the lowest is 1, because `DEY / BNE` stops
  /// before zero.
  inline constexpr std::size_t FLIGHT_KEYS_CLEARED = 56;

  void ClearFlightKeys(KeyLogger& _keys) noexcept;

  /// 6502: KY1 to KY7 -- offsets of the flight keys within `KLO`, which are their key numbers.
  inline constexpr std::size_t KEY_SLOW_DOWN = 9;   ///< 6502: KY1 -- "?"
  inline constexpr std::size_t KEY_SPEED_UP = 4;    ///< 6502: KY2 -- Space
  inline constexpr std::size_t KEY_ROLL_LEFT = 17;  ///< 6502: KY3 -- "<"
  inline constexpr std::size_t KEY_ROLL_RIGHT = 20; ///< 6502: KY4 -- ">"
  inline constexpr std::size_t KEY_PITCH_UP = 41;   ///< 6502: KY5 -- "X"
  inline constexpr std::size_t KEY_PITCH_DOWN = 51; ///< 6502: KY6 -- "S"
  inline constexpr std::size_t KEY_FIRE = 54;       ///< 6502: KY7 -- "A"

  /*
   * 6502: the five key-logger entries `TT17`'s CHART path reads, which are not `KY` anything.
   *
   * The flight keys above have names in the source because `DOKEY` reads them through labels; these
   * are written as raw offsets from `KLO` and named only in the upstream commentary, which is why
   * they arrive here as numbers with a comment rather than as constants somebody transcribed.
   *
   * THE C64 HAS ONE CURSOR KEY PER AXIS. `KLO+&3E` is "cursor left/right" and `KLO+&39` is "cursor
   * up/down" -- one key each, with SHIFT choosing the direction, which is what the two SHIFT
   * entries are for. RETURN is the accelerator: held, it multiplies the step by four.
   */
  inline constexpr std::size_t KEY_CURSOR_X = 0x3E;       ///< 6502: KLO+&3E -- cursor left/right
  inline constexpr std::size_t KEY_CURSOR_Y = 0x39;       ///< 6502: KLO+&39 -- cursor up/down
  inline constexpr std::size_t KEY_SHIFT_LEFT = 0x31;     ///< 6502: KLO+&31 -- left SHIFT
  inline constexpr std::size_t KEY_SHIFT_RIGHT = 0x0C;    ///< 6502: KLO+&C -- right SHIFT
  inline constexpr std::size_t KEY_CROSSHAIR_FAST = 0x3F; ///< 6502: KLO+&3F -- RETURN

  /*
   * 6502: CTRL -- and it is a key-logger entry like the five above, not a modifier.
   *
   * `CTRL` is one instruction, `LDX #6`, falling into `DKS4` (`LDA KEYLOOK,X / TAX / RTS`), so
   * "is CTRL held" is `KEYLOOK+6` and nothing more exotic -- `keylook.asm` names that byte "CTRL
   * is being pressed (KLO+&6)". Both readers test it with `BMI`, which is true because `RDKEY`
   * leaves a held key at 255.
   *
   * IT IS HERE BECAUSE THE PORT BELIEVED OTHERWISE. `Main.cpp` carried "CTRL is a MODIFIER, which
   * `Window` and `KeyMap` do not report -- they deliver matrix positions, and Ctrl is not one",
   * and that left the galactic hyperdrive built and unreachable from slice 4c-b onwards. Ctrl IS
   * a matrix position, and the seam could have expressed it the whole time.
   */
  inline constexpr std::size_t KEY_CONTROL = 0x06; ///< 6502: KLO+&6 -- CTRL, read by `hyp` and `TT18`

  /// 6502: what `TT17` leaves in X and Y -- one signed step per axis, four times as big with
  /// RETURN held. Zero on both when nothing is pressed, which is most passes.
  struct CrosshairStep
  {
    std::uint8_t x = 0; ///< 6502: X on return from `TT17`
    std::uint8_t y = 0; ///< 6502: Y
  };

  /*
   * 6502: TT17's `TJ1` path -- the cursor keys, as two signed steps.
   *
   * `JSTK` chooses between this and the joystick path above it, and this build's joystick path is
   * the one `TT17afterall` runs when `JSTK` is non-zero; the title screen sets `JSTK` to zero the
   * moment you dismiss it with a key rather than with fire (§6.107's other half), so a keyboard
   * player is always here.
   *
   * THE Y AXIS IS INVERTED AND THE `EOR` IS WHERE. Both axes start at 1 and become &FF when a SHIFT
   * is held, by `ORA`ing the shift entries -- and then the y one is `EOR #%11111110`, which turns 1
   * into &FF and &FF into 1. So the unshifted cursor key moves y NEGATIVE and the shifted one moves
   * it positive, the opposite way round from x.
   */
  [[nodiscard]] CrosshairStep ReadCrosshairKeys(const KeyLogger& _keys) noexcept;

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
  void ReadFlightControls(KeyLogger& _keys, ControlState& _control, const ControlOptions& _options, ShipBlock& _work, FlightState& _flight,
                          ControlEffects& _effects) noexcept;

  /*
   * 6502: SPOFF% -- the sprite pointer for the first sprite definition.
   *
   * `(SPRITELOC% - SCBASE) / 64`, and `SPRITELOC%` is `SCBASE + &2800` on this build: the sprite
   * definitions start exactly where the canvas ends, so the pointer is 160. The four the sights
   * use are 160 to 163, one per laser.
   */
  inline constexpr std::uint8_t SPRITE_POINTER_BASE = 160;

  /*
   * 6502: &63F8 and &67F8 -- where sprite 0's pointer lives, in BOTH blocks of screen RAM.
   *
   * The last eight bytes of each 1KB block of screen RAM are the VIC-II's sprite pointers, and the
   * game keeps two blocks in step because it flips the chip between them. So these are inside the
   * canvas -- offsets &23F8 and &27F8 of an array the port already compares byte for byte -- and
   * writing a sprite pointer is a canvas write like any other (§6.73).
   */
  inline constexpr std::uint16_t SIGHT_SPRITE_CELL = Canvas::SCREEN_CELLS + 0x3F8u;
  inline constexpr std::uint16_t SIGHT_SPRITE_CELL_2 = Canvas::DASHBOARD_CELLS + 0x3F8u;

  /// 6502: POW, POW+128, Armlas -- the laser powers `SIGHT` tests for, in the order it tests them.
  /// `Armlas` is `INT(128.5 + 1.5*POW)`, which is 151; the mining laser is not tested for at all
  /// and gets the fourth sprite by elimination.
  inline constexpr std::uint8_t LASER_PULSE = 15;
  inline constexpr std::uint8_t LASER_BEAM = 143;
  inline constexpr std::uint8_t LASER_MILITARY = 151;

  /// What `SIGHT` reaches that is a VIC-II register rather than memory.
  class SightEffects
  {
  public:
    virtual ~SightEffects() = default;

    /*
     * 6502: JSR SETL1 -- switch the raster interrupt handler's mode, which the routine does twice,
     * bracketing everything it touches.
     *
     * `SETL1` is self-modifying code inside the interrupt handler itself: `SEI / STA L1M / ... /
     * CLI`. §6.59 refused it a place in `GameLogic` for that reason and this is the seam it gets
     * instead. The two values are %101 on the way in and %100 on the way out.
     */
    virtual void SetRasterMode(std::uint8_t _mode) = 0;

    /// 6502: STA VIC+&27 -- sprite 0's colour, which is the sights'.
    virtual void SetSightColour(std::uint8_t _colour) = 0;

    /// 6502: STA VIC+&15 -- which of the eight sprites are switched on. Bit 0 is the sights and
    /// bits 2 to 7 are the Trumbles, which is why the two are ORed together here rather than set
    /// independently.
    virtual void SetSpritesEnabled(std::uint8_t _mask) = 0;

    /*
     * 6502: LDA VIC+&15 / AND #%00000011 / STA VIC+&15 -- and this one READS the register first.
     *
     * The flight loop's part 15 switches every sprite off but the lowest two when the cabin gets
     * hot enough to kill the Trumbles, and it does it as a read-modify-write rather than by
     * computing the new value: it does not know how many Trumbles are showing. So it cannot be
     * expressed as `SetSpritesEnabled` -- the port has no copy of the register to AND against --
     * and it is a second method rather than a getter, because a getter would invite a port to
     * compute what the hardware is holding.
     */
    virtual void MaskSprites(std::uint8_t _mask) = 0;
  };

  /*
   * 6502: SIGHT -- the laser sights and the Trumbles, which are the same four instructions apart.
   *
   * Two unrelated jobs in one routine because they share a register: bit 0 of the sprite-enable
   * byte is the sights and bits 2 to 7 are the Trumbles, so neither can be written without the
   * other. `T` carries the sights' bit across the Trumble arithmetic to the single `ORA` that
   * joins them.
   *
   * IT IS NOT ALL HARDWARE. The two sprite-pointer writes are canvas writes and `TRIBCT` is game
   * state; only the two VIC-II registers and `SETL1` are outside. §6.69 filed the whole routine as
   * a seam, which would have thrown away two thirds of it and left `LOOK1` -- which falls into
   * this -- uncomparable (§6.73).
   *
   * WITH NO LASER ON THIS VIEW IT WRITES NOTHING TO THE CANVAS. `LDA LASER,Y / BEQ SIG3` skips
   * both pointers AND the colour, so the sprite keeps whatever pointer it had and is switched off
   * instead. A port that wrote a pointer of 160 and then disabled the sprite would look the same
   * on screen and differ on every byte.
   */
  void DrawLaserSights(Canvas& _canvas, MathWorkspace& _math, const CommanderBlock& _commander, std::uint8_t& _trumbleSprites,
                       std::uint8_t _view, SightEffects& _effects) noexcept;

} // namespace Elite
