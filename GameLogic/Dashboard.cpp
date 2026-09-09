#include "pch.h"

#include "Dashboard.h"

#include "Universe.h"

#include "Arith.h"
#include "Dashboard2x.h"
#include "EliteTypes.h"
#include "LookupTables.h"

namespace Elite
{

  DangerColours DangerColour(std::uint8_t _mainLoopCounter, std::uint8_t _damageFlash) noexcept
  {
    // Bit 3 of the loop counter against the damage flash decides yellow or red -- and the
    // stray byte before the red load is what makes the yellow path skip it (§6.79's trick again).
    const std::uint8_t flashing = static_cast<std::uint8_t>(_mainLoopCounter & 0x08u & _damageFlash);

    return {(flashing != 0u) ? DIAL_NORMAL : DIAL_DANGER, DIAL_NORMAL};
  }

  void DrawBar(Canvas& _canvas, DrawWorkspace& _draw, std::uint8_t _value, int _shifts, std::uint8_t _threshold,
               DialColours _colours, Picture* _picture) noexcept
  {
    // Four shifts right, and the entry point decides how many of them run (§6.63).
    std::uint8_t value = _value;
    for (int shift = 0; shift < _shifts; ++shift)
    {
      value = static_cast<std::uint8_t>(value >> 1);
    }

    std::uint8_t pixelsLeft = value; // Into Q, which is this routine's own (M2-c)

    // &FF into R -- a full block of pixels, which the partial block below shifts down.
    std::uint8_t bits = 0xFFu;

    /*
     * DL30 and DL31 -- the value against the threshold picks which of `K` and `K+1` reaches
     * `COL`.
     *
     * Above the threshold takes `K` and below takes `K+1` -- unless `K+1` is ZERO, in which case
     * the `BNE` falls through and it takes `K` as well. `DIALS` part 1 stores `PZW`'s two colours
     * as (K, K+1) and part 3 stores them the other way round, so the same test means the opposite
     * thing for the energy bars.
     */
    PixelPattern ink = _colours.atOrAbove;
    if (value < _threshold && _colours.below != PixelPattern::Blank)
    {
      ink = _colours.below;
    }

    // Rows 2 to 4 of four character cells, so a bar is three pixels tall.
    std::uint8_t row = 2u;

    for (int block = 3; block >= 0; --block)
    {
      std::uint8_t pattern = 0;

      if (pixelsLeft >= 4u) // Q against 4, and below it goes to DL2
      {
        // A whole block lit, and the carry the comparison left makes the subtraction of four
        // exact.
        pixelsLeft = static_cast<std::uint8_t>(pixelsLeft - 4u);
        pattern = bits;
      }
      else
      {
        /*
         * DL2 and DL3 -- the count folded with 3, then a doubling loop counting it down.
         *
         * Folding with 3 is `3 - Q` for a Q below four, and the loop shifts the full block left
         * twice for each step of it -- so a Q of three lights three pixels and a Q of zero lights
         * none.
         */
        std::uint8_t remaining = static_cast<std::uint8_t>(pixelsLeft ^ 3u);
        pattern = bits;
        do
        {
          pattern = static_cast<std::uint8_t>(pattern << 2);
          remaining = static_cast<std::uint8_t>(remaining - 1u);
        } while ((remaining & 0x80u) == 0u); // Round again until the count goes negative

        // Everything past the partial block is empty, and 99 into `Q` is how the loop is
        // told there is nothing left: it can never fall below four again.
        bits = 0;
        pixelsLeft = 99u;
      }

      // The pattern masked by the colour and STORED three times over. A store rather
      // than an EOR, which is why the dashboard needs no erase and the space view does.
      const std::uint8_t byte = static_cast<std::uint8_t>(pattern & PatternByte(ink));
      for (std::uint8_t within = 0; within < 3u; ++within)
      {
        _canvas.Write(static_cast<std::uint16_t>(_draw.screenPointer + row + within), byte);
      }

      /*
       * Six added to the row index, carrying into the pointer's high byte. Y is left on the
       * block's last row, so this advances it by eight in all: one character cell to the right.
       *
       * The `INC SC+1` cannot fire. Y runs 2, 10, 18, 26 across four blocks and the add is on the
       * row after the third store, so the largest value it ever sees is 34.
       */
      row = static_cast<std::uint8_t>(row + 2u + 6u);
    }

    if (DrawingTwins(_picture))
    {
      /*
       * Thirty-two steps from the same byte with one fewer shift (Dashboard2x.h), and the
       * saturation is `DIL`'s: four blocks of four fat pixels is sixteen and no more, so four
       * blocks of eight is thirty-two and no more.
       *
       * `DILX` entered with no shift at all is the four ENERGY bars, whose value is already 0..16
       * with nothing under it -- so there the twin doubles, and the bar is the same bar at half the
       * step. The ink is `DIL`'s own: the danger flash is `PZW`'s decision (rule T1).
       */
      const int wide = (_shifts >= 1) ? static_cast<int>(_value >> (_shifts - 1)) : 2 * static_cast<int>(_value);
      DrawBar2x(*_picture, _canvas, _draw.screenPointer, (wide > 32) ? 32 : wide, ink);
    }

    // SC += 320, one character row down, ready for the next dial.
    _draw.screenPointer = static_cast<std::uint16_t>(_draw.screenPointer + 0x140u);
  }

  void DrawIndicator(Canvas& _canvas, DrawWorkspace& _draw, std::uint8_t _value, Picture* _picture, std::uint8_t _wideValue) noexcept
  {
    std::uint8_t row = 1u;   // Rows 1 to 4, so this bar is four pixels tall
    std::uint8_t pixelsLeft = _value; // Into Q, this routine's own (M2-c)

    do
    {
      std::uint8_t byte = 0;

      // Four off Q, and a borrow-free result goes to DLL11
      const SubResult step = SubtractWithCarry(pixelsLeft, 4u, true);
      if (step.carry)
      {
        pixelsLeft = step.value; // Back into Q, and an empty block
      }
      else
      {
        /*
         * The pixel picked out of `CTWOS` by the old `Q` and masked to yellow, with 255 put
         * into `Q`.
         *
         * The lit pixel, and then `Q` is set to 255 so that no later block can match -- a loop exit
         * written as data rather than as a branch.
         */
        byte = static_cast<std::uint8_t>(DASHBOARD_PIXEL_TABLE[pixelsLeft & 3u] & PatternByte(DIAL_NORMAL));
        pixelsLeft = 0xFFu;
      }

      // Four stores down the character cell.
      for (std::uint8_t within = 0; within < 4u; ++within)
      {
        _canvas.Write(static_cast<std::uint16_t>(_draw.screenPointer + row + within), byte);
      }

      // Five added to the row index -- Y is on the block's last row, so this is eight in all.
      row = static_cast<std::uint8_t>(row + 3u + 5u);
    } while (row < 30u); // DLL10, while the row is below 30

    /*
     * &13F added to the screen pointer, in two bytes.
     *
     * No clearing of the carry, and none is needed: the only way out of the loop is a comparison
     * against 30 that did not branch, so the carry is set and this adds 320 rather than 64.
     */
    if (DrawingTwins(_picture))
    {
      // The lit block is the value itself: each of the four blocks absorbs four, so a value under
      // sixteen lights fat pixel `value` and anything else lights nothing. Thirty-two slots here,
      // and the same rule.
      DrawIndicator2x(*_picture, _canvas, _draw.screenPointer, (_wideValue < 32u) ? static_cast<int>(_wideValue) : -1);
    }

    _draw.screenPointer = static_cast<std::uint16_t>(_draw.screenPointer + 0x140u);
  }

  void SetMissileIndicator(Canvas& _canvas, std::uint8_t _missile, CellPalette _palette, Picture* _picture) noexcept
  {
    // Missile 1 to 4 becomes cell 3 down to 0, so they fill from the right. The three
    // instructions around it are a register shuffle and not a use of the screen pointer.
    const std::uint8_t cell = static_cast<std::uint8_t>(static_cast<std::uint8_t>(_missile - 1u) ^ 3u);
    _canvas.Write(static_cast<std::uint16_t>(MISSILE_CELL + cell), _palette);

    if (DrawingTwins(_picture))
    {
      SetMissileIndicator2x(*_picture, _canvas, _missile);
    }
  }

  void ResetMissileIndicators(Canvas& _canvas, std::uint8_t _missiles, Picture* _picture) noexcept
  {
    // From 4 downwards, black, until the index meets `NOMSL`.
    std::uint8_t indicator = 4u;
    while (indicator != _missiles && indicator != 0u)
    {
      SetMissileIndicator(_canvas, indicator, MISSILE_NONE, _picture);
      --indicator;
    }

    // Green from there, on the same index, carrying on downwards.
    while (indicator != 0u)
    {
      SetMissileIndicator(_canvas, indicator, MISSILE_READY, _picture);
      --indicator;
    }
  }

  void SetMissileTarget(Universe& _universe, std::uint8_t _missiles, std::uint8_t _target, CellPalette _palette) noexcept
  {
    _universe.bubble.missileTarget = _target;                                       // Into MSTG
    SetMissileIndicator(_universe.canvas, _missiles, _palette, &_universe.backdrop); // MSBAR on the missile count

    // Into MSAR -- and Y is the ZERO `MSBAR` ended on, not the colour that went in.
    _universe.status.missileArmed = 0;
  }

  void AbortMissileLock(Universe& _universe, std::uint8_t _missiles, CellPalette _palette) noexcept
  {
    // An index of 255, and no return: it runs straight into ABORT2.
    SetMissileTarget(_universe, _missiles, 0xFFu, _palette);
  }

  void ToggleEcmIndicator(Canvas& _canvas, Picture* _picture) noexcept
  {
    // Two cells, one above the other, EORed in and out.
    _canvas.ExclusiveOr(ECM_CELL, BULB_COLOUR);
    _canvas.ExclusiveOr(static_cast<std::uint16_t>(ECM_CELL + 40u), BULB_COLOUR);

    if (DrawingTwins(_picture))
    {
      ToggleBulb2x(*_picture, _canvas, ECM_CELL);
    }
  }

  void ToggleStationIndicator(Canvas& _canvas, Picture* _picture) noexcept
  {
    _canvas.ExclusiveOr(STATION_CELL, BULB_COLOUR);
    _canvas.ExclusiveOr(static_cast<std::uint16_t>(STATION_CELL + 40u), BULB_COLOUR);

    if (DrawingTwins(_picture))
    {
      ToggleBulb2x(*_picture, _canvas, STATION_CELL);
    }
  }

  void StartEcm(Canvas& _canvas, FlightStatus& _status, SoundBuffer& _sound, bool _carryIn, Picture* _picture) noexcept
  {
    /*
     * The countdown set and the effect played, and NOT ONE OF THOSE INSTRUCTIONS
     * TOUCHES THE CARRY. So the flag `NOISE` sees is the one this routine was CALLED with, which is
     * why it is an argument and not a constant: the pass-through §6.99 found at the seam runs
     * through the
     * routine above it too (§6.118).
     */
    _status.ecmCountdown = 32u;                                // 32 into ECMA
    (void)PlaySoundEffect(_sound, SoundEffect::Ecm, _carryIn); // NOISE with sfxecm
    ToggleEcmIndicator(_canvas, _picture);                     // And no RTS -- it falls into ECBLB
  }

  void StopEcm(Canvas& _canvas, FlightStatus& _status, SoundBuffer& _sound, Picture* _picture) noexcept
  {
    _status.ecmCountdown = 0u;                 // Zero into ECMA
    _status.ecmOurs = 0u;                      // And into ECMP
    ToggleEcmIndicator(_canvas, _picture);
    StopSoundEffect(_sound, SoundEffect::Ecm); // NOISEOFF with sfxecm, a tail call, so this ends it
  }

  void DrawDials(Canvas& _canvas, DrawWorkspace& _draw, const FlightState& _flight, const FlightStatus& _status, LightYearsTenths _fuel,
                 Compass& _compass, const Bubble& _bubble, Picture* _picture) noexcept
  {
    // ---- part 1: the speed bar ------------------------------------------------------------------

    // The screen pointer thirty character cells into the dashboard.
    _draw.screenPointer = static_cast<std::uint16_t>(DASHBOARD_BITMAP + 8u * 30u);

    // The danger colour in `K` and yellow in `K+1`.
    const DangerColours danger = DangerColour(_flight.mainLoopCounter, _status.damageFlash);
    const DialColours speedColours{danger.a, danger.x};

    // A threshold of 14 into `T1`, then the speed through `DIL-1`.
    DrawBar(_canvas, _draw, _flight.speed, 1, 14u, speedColours, _picture);

    // ---- part 2: roll and pitch -----------------------------------------------------------------

    // `ADD` adds a positive eight to whatever sign-magnitude byte it is handed, which is
    // what centres both indicators.
    // (A P) is the value over a zero low byte, and (S R) is eight -- the centre of the indicator.
    constexpr SignMag16 INDICATOR_CENTRE{0u, 8u};

    /*
     * The roll magnitude quartered, its sign ORed back, flipped, then `ADD` and `DIL2`.
     *
     * The roll magnitude quartered, its sign put back, and then the sign FLIPPED -- because the
     * indicator moves the other way from the roll.
     */
    const std::uint8_t roll = static_cast<std::uint8_t>(((_flight.rollMagnitude >> 2) | _flight.rollSign) ^ 0x80u);

    /*
     * And the same again with ONE fewer shift, which is the bit the dial throws away.
     *
     * `alp1` is the roll magnitude and the two shifts drop two of its bits before the indicator
     * ever sees it, so `alp1 >> 1` is one of them back -- and the centre doubles with it, because
     * the wide dial has thirty-two slots where this one has sixteen. Nothing faithful reads this
     * (Resolution.md section 5.1, and rule T2: the same arithmetic at twice the scale).
     */
    constexpr SignMag16 WIDE_CENTRE{0u, 16u};
    const std::uint8_t wideRoll = static_cast<std::uint8_t>(((_flight.rollMagnitude >> 1) | _flight.rollSign) ^ 0x80u);

    DrawIndicator(_canvas, _draw, AddSigned(SignMag16{0u, roll}, INDICATOR_CENTRE).high, _picture,
                  AddSigned(SignMag16{0u, wideRoll}, WIDE_CENTRE).high);

    /*
     * The pitch, less one when `BET1` is not zero, then `ADD` and `DIL2`.
     *
     * That subtraction HAS NO SET OF THE CARRY in front of it, so it runs on the one `DIL2` left --
     * and `DIL2` ends by adding one to a screen-address high byte, which cannot carry out. So
     * the carry is always CLEAR and the pitch indicator is offset by TWO rather than by one.
     *
     * The fourteenth uncleared flag, and UNLIKE the thirteenth it is load-bearing: `SP2`'s
     * addition of 195 could not see an always-clear carry and this subtraction borrows because of
     * it, so the
     * mutation that assumes a set carry moves the indicator and the mutation that assumed one in
     * `SP2` was equivalent. Constant does not mean invisible, and which of the two it is depends on
     * the instruction rather than on the flag (§6.65).
     */
    std::uint8_t pitch = _flight.pitchRate;
    if (_flight.pitchMagnitude != 0u)
    {
      pitch = SubtractWithCarry(pitch, 1u, false).value;
    }
    /*
     * The pitch has NO bit to recover, and saying so is the point (Dashboard2x.h). `beta` is `bet1`
     * with its sign on, and `bet1` was shifted down four places in the flight loop within four
     * instructions of the joystick -- so by the time the dial sees it the bit is game state that no
     * longer exists, and recovering it would mean recomputing the roll rate rather than drawing it.
     * The wide value is therefore the faithful one at twice the scale and no finer.
     */
    DrawIndicator(_canvas, _draw, AddSigned(SignMag16{0u, pitch}, INDICATOR_CENTRE).high, _picture,
                  AddSigned(SignMag16{0u, DoubledMagnitude(pitch)}, WIDE_CENTRE).high);

    // ---- part 3: the four energy bars, on one pass in four --------------------------------------

    // Part 3's threshold into `T1`, and part 4 is only ever reached through part 3,
    // so the shields and the fuel are drawn against it too.
    constexpr std::uint8_t BAR_THRESHOLD = 3u;

    /*
     * The loop counter masked to two bits, and anything but zero goes to `dec27`.
     *
     * `dec27` is `TT26`'s own `RTS` borrowed as a branch target, so this does not skip part 3 -- it
     * RETURNS FROM `DIALS`. Three passes in four draw the speed, the roll and the pitch and stop
     * there: the energy bars, the shields, the fuel, the two temperatures, the altitude and the
     * compass are all one pass in four (§6.64).
     */
    if ((_flight.mainLoopCounter & 3u) != 0u)
    {
      return;
    }

    {
      // PZW again, stored the OTHER way round from part 1, so the same threshold test in
      // `DIL` picks the opposite colour.
      const DangerColours bars = DangerColour(_flight.mainLoopCounter, _status.damageFlash);
      const DialColours barColours{bars.x, bars.a};

      // All four cleared before any is read.
      // The four bytes are `XX12`, and this routine's own (M2-c).
      std::array<std::uint8_t, 4> dotProducts{};

      /*
       * The energy quartered, then sixteen taken off at a time until it borrows.
       *
       * The energy quartered and then dealt out sixteen at a time from the TOP bar downwards, so a
       * full bank fills bar 3 first and the remainder lands in whichever bar the subtraction ran
       * out on.
       */
      std::uint8_t energyLeft = static_cast<std::uint8_t>(_status.energy >> 2); // Into Q
      int bar = 3;
      for (;;)
      {
        const SubResult left = SubtractWithCarry(energyLeft, 16u, true);
        if (!left.carry)
        {
          dotProducts[static_cast<std::size_t>(bar)] = energyLeft;
          break;
        }

        energyLeft = left.value;
        dotProducts[static_cast<std::size_t>(bar)] = 16u;
        --bar;
        if (bar < 0)
        {
          break; // Out of DLL24 and into DLL9
        }
      }

      // The four bars in turn, through `DIL` and not `DILX`, so they are drawn
      // unshifted. `P` parks the index across the call and nothing else reads it.
      for (std::uint8_t which = 0; which < 4u; ++which)
      {
        DrawBar(_canvas, _draw, dotProducts[which], 0, BAR_THRESHOLD, barColours, _picture);
      }
    }

    // ---- part 4: the shields, the fuel, the temperatures and the altitude ------------------------

    // The screen pointer back to the left-hand column.
    _draw.screenPointer = static_cast<std::uint16_t>(DASHBOARD_BITMAP + 8u * 6u);

    // Both colours the same, so the shields and the fuel do
    // not flash whatever `T1` says -- and `T1` is still part 3's 3.
    const DialColours plain{DIAL_NORMAL, DIAL_NORMAL};

    DrawBar(_canvas, _draw, _status.forwardShield, 4, BAR_THRESHOLD, plain, _picture);
    DrawBar(_canvas, _draw, _status.aftShield, 4, BAR_THRESHOLD, plain, _picture);
    DrawBar(_canvas, _draw, _fuel.tenths, 2, BAR_THRESHOLD, plain, _picture);

    // PZW in part 1's order again, so the temperatures flash.
    const DangerColours heat = DangerColour(_flight.mainLoopCounter, _status.damageFlash);
    const DialColours heatColours{heat.a, heat.x};

    // A threshold of 11 into `T1`
    DrawBar(_canvas, _draw, _status.cabinTemperature, 4, 11u, heatColours, _picture);
    DrawBar(_canvas, _draw, _status.laserTemperature, 4, 11u, heatColours, _picture);

    // A threshold of 240 -- the altitude never reaches it, so it never flashes.
    DrawBar(_canvas, _draw, _status.altitude, 4, 240u, heatColours, _picture);

    UpdateCompass(_canvas, _compass, _bubble, _picture); // COMPAS, as a tail call
  }

} // namespace Elite
