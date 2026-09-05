#pragma once

#include <array>
#include <cstdint>

#include "Canvas.h"
#include "ShipSlot.h"

namespace Elite
{

  struct MathWorkspace;

  /*
   * The scanner and the compass (slice 3d-a).
   *
   * Two things that look unrelated and are not: both turn a position in the bubble into a mark on
   * the dashboard, both are drawn with EOR so that the next frame erases them by drawing them
   * again, and both are reached from the flight loop on every pass. `SCAN` does it for each ship,
   * `COMPAS` for whichever of the planet and the space station the compass is pointing at.
   *
   * They are the first of slice 3d because they need nothing that is not already built -- `CPIX2`,
   * `CPIX4`, `CTWOS2` and `DVID4` -- and because `SCAN` closes two seams that two ported units
   * have been reaching through since 3a (§6.59).
   */

  // ---- the scanner ----------------------------------------------------------------------------

  /*
   * 6502: SCAN -- draw or erase one ship's blip.
   *
   * A BLIP IS A DOT AND A STICK. The dot is `CPIX4` at the ship's position on the scanner's
   * ellipse; the stick joins it to the horizontal line the ellipse is drawn around, so the height
   * of the stick is how far above or below the plane of flight the ship is. It is drawn with EOR,
   * so calling this twice with the same block leaves the screen as it was -- which is the whole of
   * how the game erases a blip.
   *
   * `_type` and `_view` are `TYPE` and `QQ11`, and they are arguments because they are GLOBALS the
   * routine reads rather than anything it is given: `WPSHPS` writes `TYPE` from the slot before
   * each call, and `MVEIT` does not write it at all -- the flight loop did, before it called
   * `MVEIT`. Two seams disagreed about this until 3d-a collapsed them into this one function
   * (§6.59): one passed the type and one did not, and neither passed the view.
   *
   * IT DOES NOT MOVE THE SHIP. The block is read and not written, which is why the argument is
   * const -- `WPSHPS` clears bits 3, 4 and 6 of byte 31 itself, in the SLOT rather than in `INWK`,
   * after this returns.
   *
   * WHAT IT LEAVES BEHIND: `X1` is the stick's pixel mask and not an x coordinate by the time this
   * returns, `Y1` is one less than the blip's row because `CPIX4` decrements it, and `COL` is the
   * type's scanner colour. All three are the original's, and `SCAN` is a leaf that nobody reads
   * them back from -- but the port keeps them because it keeps the workspace.
   */
  void DrawScannerBlip(Canvas& _canvas, DrawWorkspace& _work, const ShipBlock& _ship, std::uint8_t _type, std::uint8_t _view) noexcept;

  // ---- the compass ----------------------------------------------------------------------------

  /// 6502: YELLOW -- four multicolour pixels of colour %10, and the compass's "ahead" colour. It
  /// is also what makes the dot a four-pixel block rather than a dash, because `DOT` branches on
  /// the colour and not on the direction.
  inline constexpr std::uint8_t COMPASS_AHEAD = 0xAA;

  /// 6502: GREEN -- four pixels of %11, and "behind": a two-pixel dash rather than a block.
  inline constexpr std::uint8_t COMPASS_BEHIND = 0xFF;

  /// 6502: COMX, COMY and COMC -- where the compass dot is and what colour it is. They persist
  /// between frames because that is how it is erased: `COMPAS` draws the OLD dot again before it
  /// works out the new one.
  struct Compass
  {
    std::uint8_t x = 0;      ///< 6502: COMX
    std::uint8_t y = 0;      ///< 6502: COMY
    std::uint8_t colour = 0; ///< 6502: COMC
  };

  /*
   * 6502: K3 -- the ten bytes `SPS1`, `SPS3`, `SPS4` and `TAS2` pass between them.
   *
   * Three (low, high, sign) triples at 0, 3 and 6, and a tenth byte at 9 that `TAS2` builds for
   * itself. It is an array rather than three named fields because `SPS3` writes `K3,X`, `K3+1,X`
   * and `K3+2,X` with X in a register -- §6.37's rule, the same one that made `XX15` two fields
   * and `XX16` an array.
   *
   * It is scratch with no life outside the routine that fills it, so `UpdateCompass` declares one
   * and the routines below take it; nothing keeps one.
   *
   * IT WAS CALLED `CompassAxes` UNTIL 2026-09-05, and the old comment said why that was safe: the
   * original shares this zero page with `CIRCLE2` and `TACTICS`, "which are never live at the same
   * time". True, and the name still stopped being right the moment phase 4 started -- `TAS1`,
   * `VCSUB` and `DCS1` fill the same ten bytes with a vector that has nothing to do with a compass.
   * The original's own name is the one that survives both callers (§6.121).
   */
  using K3Block = std::array<std::uint8_t, 10>;

  /*
   * 6502: DOT -- draw the compass dot where `COMX`, `COMY` and `COMC` say it is.
   *
   * The colour decides the SHAPE. `CMP #YELLOW / BNE CPIX2` falls through into `CPIX4` when the
   * target is ahead and branches to `CPIX2` when it is behind, so a dot pointing forwards is a
   * four-pixel block and one pointing backwards is a two-pixel dash. That is not a separate
   * decision from the colour: it is the same byte read twice.
   */
  void DrawCompassDot(Canvas& _canvas, DrawWorkspace& _work, const Compass& _compass) noexcept;

  /*
   * 6502: SPS3 -- copy one of the planet's coordinates into `K3`, as (mid, high, sign).
   *
   * IT DROPS THE LOW BYTE, and that is the point. The planet's coordinates are 24-bit -- byte 0 is
   * the low byte, byte 1 the middle, and byte 2 carries the sign in bit 7 and the top seven bits
   * below it -- so this takes bytes 1 and 2 as a sixteen-bit magnitude and keeps the sign apart.
   * The planet is millions of units away; the bottom eight bits of that are not a direction.
   */
  void LoadPlanetAxis(const ShipBlock& _planet, K3Block& _axes, std::uint8_t _at) noexcept;

  /*
   * 6502: TAS2 -- turn the three coordinates in `K3` into a unit vector in `XX15`.
   *
   * Shift all three left together until the largest of them overflows out of bit 7, then take each
   * high byte, halve it, and put the sign back on top -- so what comes out is three sign-magnitude
   * bytes with seven bits of magnitude, pointing the same way the input did.
   *
   * AND THEN IT FALLS INTO `NORM`, which is why this takes a `MathWorkspace`: `TAS2` has no `RTS`,
   * so the three bytes the shifting produces are an intermediate and the answer is that vector
   * scaled to a length of 96. Slice 3d-a's first sweep caught it -- `K3` agreed byte for byte and
   * `XX15` did not, which is the signature of a fall-through rather than of arithmetic (§6.62).
   *
   * THE TENTH BYTE IS THE SHIFT COUNTER, spelled as data. `K3+9` starts as the three low bytes
   * ORed together with bit 0 forced on, and the loop rotates it and the ORed high bytes as one
   * sixteen-bit value: the forced bit is what guarantees the loop ends, because it reaches bit 7
   * and falls out within sixteen turns however small the coordinates are.
   *
   * `XX15` here is `X1`, `Y1` and `X2` -- the same six bytes the line drawing uses, because that is
   * what `XX15` is (§6.37). `SP2` reads all three back.
   */
  void NormaliseAxes(K3Block& _axes, DrawWorkspace& _work, MathWorkspace& _math) noexcept;

  /// 6502: SPS1 -- three `SPS3` calls for the planet, then a fall-through into `TAS2`. The
  /// fall-through is the routine: `SPS1` has no `RTS` of its own.
  void LoadPlanetAxes(const Bubble& _bubble, K3Block& _axes, DrawWorkspace& _work, MathWorkspace& _math) noexcept;

  /*
   * 6502: SPS4 -- the same for the space station, which is nine bytes copied straight across.
   *
   * SLOT 1, always: `K%+NI%` is the second ship block, and that is where `NWSPS` puts the station
   * -- over the sun, which is why the two are never in the bubble together. And the station's
   * coordinates are ordinary sixteen-bit ones, so unlike the planet's there is nothing to drop.
   */
  void LoadStationAxes(const Bubble& _bubble, K3Block& _axes, DrawWorkspace& _work, MathWorkspace& _math) noexcept;

  /// What `SPS2` hands back: the original returns the signed offset in X and its sign extension in
  /// Y, and `SP2` reads both -- plus the carry, which comes from `DVID4` and lands in an `ADC` and
  /// an `SBC` with nothing clearing it in between (§6.60).
  struct CompassOffset
  {
    std::uint8_t offset = 0; ///< 6502: X -- the position, in scanner pixels, as a two's complement byte
    std::uint8_t sign = 0;   ///< 6502: Y -- 0 or 255, the same sign as a mask
    bool carry = false;      ///< 6502: the carry `DVID4` left, which `SP2` adds and subtracts with
  };

  /*
   * 6502: SPS2 -- turn one of `TAS2`'s sign-magnitude bytes into a signed offset from the centre
   * of the compass, which is A * 2 / 20 with the sign put back on.
   *
   * The twenty is the compass's radius in pixels. The doubling is not a scale factor the divide
   * then undoes -- it is how the sign gets out of the way: `ASL A` pushes bit 7 into the carry and
   * `LDA #0 / ROR A` catches it, leaving the magnitude in A with nothing above it.
   */
  [[nodiscard]] CompassOffset ScaleToCompass(MathWorkspace& _math, std::uint8_t _a) noexcept;

  /*
   * 6502: SP2 -- put the dot where `XX15` points, and draw it.
   *
   * `JMP DOT` at the end, so drawing is part of it rather than something the caller does after.
   * The colour comes from the sign of the third coordinate alone: ahead is yellow and behind is
   * green, and `DOT` reads that same byte again to decide whether to draw a block or a dash.
   */
  void DrawCompass(Canvas& _canvas, DrawWorkspace& _work, MathWorkspace& _math, Compass& _compass) noexcept;

  /// 6502: SP1 -- `JSR SPS4` and then a fall-through into `SP2`. Aim the compass at the station
  /// and draw it.
  void AimCompassAtStation(Canvas& _canvas, DrawWorkspace& _work, MathWorkspace& _math, Compass& _compass, const Bubble& _bubble,
                           K3Block& _axes) noexcept;

  /*
   * 6502: COMPAS -- erase the old dot, work out the new one, draw it.
   *
   * The first `JSR DOT` is the erase, and it works because everything here is EOR: the dot is
   * still where the last frame left it, so drawing it again takes it away. That is also why
   * `Compass` has to persist between calls.
   *
   * `SSPR` chooses the target, and `SSPR` is `MANY + SST` -- the space station's entry in the ship
   * count, not a flag of its own (§6.58). So the compass points at the station whenever there is
   * one and at the planet otherwise.
   */
  void UpdateCompass(Canvas& _canvas, DrawWorkspace& _work, MathWorkspace& _math, Compass& _compass, const Bubble& _bubble) noexcept;

} // namespace Elite
