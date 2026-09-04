#include "pch.h"

#include "Stardust.h"

#include "EliteTypes.h"

namespace Elite
{

  /*
   * The wrappers.
   *
   * Every one of these is a `LDA` or two and then a fall-through into a routine the port already
   * has. They are named separately because that is what they are in the original -- each has its
   * own include file and its own entry point, and the movers below `JSR` to them by name.
   */

  std::uint8_t DivideSpeedBy(MathWorkspace& _math, const FlightState& _flight, std::uint8_t _a) noexcept
  {
    _math.q = _a;

    // 6502: DVID4's exit carry is the divide's saturation flag and only `SPS2` reads it -- the
    // stardust follows this with `LSR P`, which makes its own (§6.60).
    return DivideAndScale(_math, _flight.delta).r;
  }

  std::uint8_t DivideSpeedByDistance(MathWorkspace& _math, const FlightState& _flight, const Stardust& _dust, std::uint8_t _at) noexcept
  {
    return DivideSpeedBy(_math, _flight, _dust.z[_at]);
  }

  WideResult MultiplyByHeight(MathWorkspace& _math, DrawWorkspace& _draw, const Stardust& _dust, std::uint8_t _at) noexcept
  {
    _draw.y1 = _dust.y[_at];
    return MultiplyMagnitudeByQ(_math, _draw.y1);
  }

  std::uint8_t MultiplyScaledBy(MathWorkspace& _math, std::uint8_t _x, std::uint8_t _a) noexcept
  {
    // 6502: MULTS-2, which is the `STX P` two bytes before MULTS. The movers reach it with the
    // multiplier already in X, so there is nothing else to do.
    _math.p = _x;
    return MultiplyScaled(_math, _a);
  }

  std::uint8_t MultiplyByRoll(MathWorkspace& _math, const FlightState& _flight, std::uint8_t _a) noexcept
  {
    return MultiplyScaledBy(_math, _flight.alp1, _a);
  }

  std::uint8_t MultiplyPositionByRoll(MathWorkspace& _math, const FlightState& _flight, std::uint8_t _a) noexcept
  {
    _math.r = _math.xx;
    _math.s = _math.xxNext;
    return MultiplyByRoll(_math, _flight, _a);
  }

  std::uint8_t MultiplyPosition(MathWorkspace& _math, std::uint8_t _a) noexcept
  {
    _math.r = _math.xx;
    return MultiplySigned(_math, _a);
  }

  std::uint8_t MultiplyPositionSigned(MathWorkspace& _math, std::uint8_t _a) noexcept
  {
    // 6502: MUT2 falls into MUT1, so it sets BOTH halves and not just S.
    _math.s = _math.xxNext;
    return MultiplyPosition(_math, _a);
  }

  void PlotStardust(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, Stardust& _dust, std::uint8_t _at,
                    std::uint8_t _a) noexcept
  {
    const AddSignedResult sum = AddSigned(_math, _a);
    _math.yyNext = sum.high;
    _dust.yLow[_at] = sum.low;
    (void)PlotRelativePixel(_canvas, _draw);
  }

  void FlipStardust(Canvas& _canvas, DrawWorkspace& _draw, Stardust& _dust) noexcept
  {
    for (std::uint8_t at = _dust.count; at != 0u; --at)
    {
      const std::uint8_t was = _dust.y[at];
      _draw.y1 = _dust.x[at];
      _dust.y[at] = _draw.y1;
      _draw.x1 = was;
      _dust.x[at] = was;
      _draw.zz = _dust.z[at];

      (void)PlotRelativePixel(_canvas, _draw);
    }
  }

  namespace
  {

    /// 6502: the `LSR P / ROR A` pair the front and rear views open with -- two shifts of a
    /// sixteen-bit value held across P and A, which halve the reciprocal of the distance twice.
    std::uint8_t HalveTwice(MathWorkspace& _math, std::uint8_t _a, bool& _carry) noexcept
    {
      for (int shift = 0; shift < 2; ++shift)
      {
        const bool into = (_math.p & 0x01u) != 0u;
        _math.p = static_cast<std::uint8_t>(_math.p >> 1);
        const ShiftResult rotated = RotateRight(_a, into);
        _a = rotated.value;
        _carry = rotated.carry;
      }
      return _a;
    }

    /// 6502: `ASL P / ROL A / STA T / LDA #0 / ROR A / ORA T` -- the front and rear views both end
    /// their pitch step with this, which doubles (A P) and folds the bit that fell off the top back
    /// in as a sign.
    std::uint8_t DoubleAndFold(MathWorkspace& _math, std::uint8_t _a) noexcept
    {
      const ShiftResult low = RotateLeftValue(_math.p, false);
      _math.p = low.value;
      const ShiftResult high = RotateLeft(_a, low.carry);

      return static_cast<std::uint8_t>(RotateRight(0, high.carry).value | high.value);
    }

  } // namespace

  void MoveStardustAhead(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, const FlightState& _flight, Stardust& _dust,
                         Rng& _rng) noexcept
  {
    for (std::uint8_t at = _dust.count; at != 0u; --at)
    {
      // 6502: STL1 -- the speed over the distance, halved twice, is how far this speck moves.
      // The `ORA #1` stops a distant speck dividing by zero further down.
      std::uint8_t a = DivideSpeedByDistance(_math, _flight, _dust, at);
      bool carry = false;
      a = HalveTwice(_math, a, carry);
      _math.q = static_cast<std::uint8_t>(a | 1u);

      // The speck comes towards you, so its distance falls by four times the speed. The borrow
      // this subtraction runs on is the one the second `ROR A` above left, not a `SEC`.
      const SubResult zLow = SubtractWithCarry(_dust.zLow[at], _flight.delt4, carry);
      _dust.zLow[at] = zLow.value;
      _draw.zz = _dust.z[at];
      _dust.z[at] = SubtractWithCarry(_dust.z[at], _flight.delt4Next, zLow.carry).value;

      // Its height and its distance across, each scaled by how much closer it now is.
      const WideResult height = MultiplyByHeight(_math, _draw, _dust, at);
      _math.yyNext = height.high;
      const AddResult yLow = AddWithCarry(_math.p, _dust.yLow[at], height.carry);
      _math.yy = yLow.value;
      _math.r = yLow.value;
      _math.yyNext = AddWithCarry(_draw.y1, _math.yyNext, yLow.carry).value;
      _math.s = _math.yyNext;

      _draw.x1 = _dust.x[at];
      const WideResult across = MultiplyMagnitudeByQ(_math, _draw.x1);
      _math.xxNext = across.high;
      const AddResult xLow = AddWithCarry(_math.p, _dust.xLow[at], across.carry);
      _math.xx = xLow.value;
      _math.xxNext = AddWithCarry(_draw.x1, _math.xxNext, xLow.carry).value;

      // 6502: the roll, as two multiply-and-adds with the signs crossed over.
      std::uint8_t rolled = MultiplyByRoll(_math, _flight, static_cast<std::uint8_t>(_math.xxNext ^ _flight.alp2Next));
      AddSignedResult sum = AddSigned(_math, rolled);
      _math.yyNext = sum.high;
      _math.yy = sum.low;

      rolled = MultiplyPositionByRoll(_math, _flight, static_cast<std::uint8_t>(sum.high ^ _flight.alp2));
      sum = AddSigned(_math, rolled);
      _math.xxNext = sum.high;
      _math.xx = sum.low;

      // And the pitch.
      _math.q = MultiplyScaledBy(_math, _flight.bet1, static_cast<std::uint8_t>(_math.yyNext ^ _flight.bet2Next));
      const std::uint8_t pitched = MultiplyPositionSigned(_math, _math.q);
      sum = AddSigned(_math, DoubleAndFold(_math, pitched));
      _math.xxNext = sum.high;
      _dust.xLow[at] = sum.low;

      _math.r = _math.yy;
      _math.s = _math.yyNext;
      _math.p = 0;
      PlotStardust(_canvas, _draw, _math, _dust, at, static_cast<std::uint8_t>(_flight.beta ^ 0x80u));

      // 6502: the three kill tests. A speck that has drifted more than 120 either way, or come
      // closer than 16, is not clipped -- it is thrown away and a new one rolled at the edge.
      _draw.x1 = _math.xxNext;
      _dust.x[at] = _math.xxNext;

      /*
       * Three kill tests, and WHICH of them fires decides the carry the first `DORND` below runs
       * on: the two `CMP #120 / BCS` tests arrive with it set and the `CMP #16 / BCC` with it
       * clear. The generator takes the carry as an operand, so the speck that replaces one which
       * drifted sideways is a different speck from the one replacing a speck that came too close.
       */
      bool killed = (_math.xxNext & 0x7Fu) >= 120u;
      bool entryCarry = true;

      if (!killed)
      {
        _dust.y[at] = _math.yyNext;
        _draw.y1 = _math.yyNext;

        killed = (_math.yyNext & 0x7Fu) >= 120u;
        if (!killed && _dust.z[at] < 16u)
        {
          killed = true;
          entryCarry = false;
        }
      }

      if (killed)
      {
        // 6502: KILL1 -- and each `DORND` after the first runs on the one before it, because
        // nothing between them touches the carry.
        RngResult roll = _rng.Next(entryCarry);
        _draw.y1 = static_cast<std::uint8_t>(roll.value | 4u);
        _dust.y[at] = _draw.y1;

        roll = _rng.Next(roll.carry);
        _draw.x1 = static_cast<std::uint8_t>(roll.value | 8u);
        _dust.x[at] = _draw.x1;

        roll = _rng.Next(roll.carry);
        _draw.zz = static_cast<std::uint8_t>(roll.value | 144u);
        _dust.z[at] = _draw.zz;
      }
      else
      {
        _draw.zz = _dust.z[at];
      }

      (void)PlotRelativePixel(_canvas, _draw);
    }
  }

  void MoveStardustAstern(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, const FlightState& _flight, Stardust& _dust,
                          Rng& _rng) noexcept
  {
    for (std::uint8_t at = _dust.count; at != 0u; --at)
    {
      // 6502: STL6 -- the same opening as the front view, down to the `ORA #1`.
      std::uint8_t a = DivideSpeedByDistance(_math, _flight, _dust, at);
      bool carry = false;
      a = HalveTwice(_math, a, carry);
      (void)carry; // the front view's next instruction is an `SBC`; this one's is a `JSR`.
      _math.q = static_cast<std::uint8_t>(a | 1u);

      /*
       * Looking backwards the dust recedes, so every step the front view adds this one subtracts
       * and the distance goes UP rather than down. It is not the same routine with a sign, though:
       * the coordinates are done in the other order, the roll's two sign bytes are swapped, the
       * pitch is not negated, and both kill tests are different. Two routines, deliberately.
       */
      _draw.x1 = _dust.x[at];
      const WideResult across = MultiplyMagnitudeByQ(_math, _draw.x1);
      _math.xxNext = across.high;
      const SubResult xLow = SubtractWithCarry(_dust.xLow[at], _math.p, across.carry);
      _math.xx = xLow.value;
      const SubResult xHigh = SubtractWithCarry(_draw.x1, _math.xxNext, xLow.carry);
      _math.xxNext = xHigh.value;

      const WideResult height = MultiplyByHeight(_math, _draw, _dust, at);
      _math.yyNext = height.high;
      const SubResult yLow = SubtractWithCarry(_dust.yLow[at], _math.p, height.carry);
      _math.yy = yLow.value;
      _math.r = yLow.value;
      const SubResult yHigh = SubtractWithCarry(_draw.y1, _math.yyNext, yLow.carry);
      _math.yyNext = yHigh.value;
      _math.s = yHigh.value;

      // 6502: `ADC DELT4` -- on the borrow the subtraction above left, with no `CLC` between them.
      const AddResult zLow = AddWithCarry(_dust.zLow[at], _flight.delt4, yHigh.carry);
      _dust.zLow[at] = zLow.value;
      _draw.zz = _dust.z[at];
      _dust.z[at] = AddWithCarry(_dust.z[at], _flight.delt4Next, zLow.carry).value;

      // The roll, and the two sign bytes are the other way round from the front view's -- which is
      // the whole of what makes the dust roll the opposite way when you look behind you.
      std::uint8_t rolled = MultiplyByRoll(_math, _flight, static_cast<std::uint8_t>(_math.xxNext ^ _flight.alp2));
      AddSignedResult sum = AddSigned(_math, rolled);
      _math.yyNext = sum.high;
      _math.yy = sum.low;

      rolled = MultiplyPositionByRoll(_math, _flight, static_cast<std::uint8_t>(sum.high ^ _flight.alp2Next));
      sum = AddSigned(_math, rolled);
      _math.xxNext = sum.high;
      _math.xx = sum.low;

      /*
       * And the pitch, where the two routines diverge further than a sign: `STARS1` squares the
       * pitch term (`STA Q / JSR MUT2` leaves A holding what it just stored), and this one
       * multiplies it by the negated x instead. The port keeps both as written (ADR-003).
       */
      _math.q = MultiplyScaledBy(_math, _flight.bet1, static_cast<std::uint8_t>(_math.yyNext ^ _flight.bet2Next));
      _math.s = _math.xxNext;
      const std::uint8_t pitched = MultiplyPosition(_math, static_cast<std::uint8_t>(_math.xxNext ^ 0x80u));
      sum = AddSigned(_math, DoubleAndFold(_math, pitched));
      _math.xxNext = sum.high;
      _dust.xLow[at] = sum.low;

      _math.r = _math.yy;
      _math.s = _math.yyNext;
      _math.p = 0;
      PlotStardust(_canvas, _draw, _math, _dust, at, _flight.beta);

      _draw.x1 = _math.xxNext;
      _dust.x[at] = _math.xxNext;
      _draw.y1 = _math.yyNext;
      _dust.y[at] = _math.yyNext;

      /*
       * Two kill tests rather than three, and both on the values just stored: more than 110 up or
       * down, or further away than 160. There is no test on x at all -- a speck can drift as far
       * sideways as the arithmetic takes it, because the projection has already wrapped it.
       *
       * Both arrive at the generator with carry SET, so `KILL6` has one entry where `KILL1` has
       * two.
       */
      const bool killed = (_math.yyNext & 0x7Fu) >= 110u || _dust.z[at] >= 160u;

      if (killed)
      {
        // 6502: KILL6 -- a new distance first, between 10 and 137, and its own bottom bits then
        // choose which edge the speck comes back in at. Free randomness the generator is not
        // called a fourth time for.
        RngResult roll = _rng.Next(true);
        const AddResult renewed = AddWithCarry(static_cast<std::uint8_t>(roll.value & 0x7Fu), 10u, roll.carry);
        _dust.z[at] = renewed.value;
        _draw.zz = renewed.value;

        if (RotateRight(renewed.value, false).carry)
        {
          // 6502: ST4 -- anywhere across, at the top or the bottom.
          roll = _rng.Next(true);
          _draw.x1 = roll.value;
          _dust.x[at] = roll.value;

          _draw.y1 = RotateRight(230u, RotateRight(roll.value, false).carry).value;
          _dust.y[at] = _draw.y1;
        }
        else
        {
          // 6502: at the left or the right edge, anywhere up or down.
          const bool side = RotateRight(static_cast<std::uint8_t>(renewed.value >> 1), false).carry;
          _draw.x1 = RotateRight(252u, side).value;
          _dust.x[at] = _draw.x1;

          // The `ROR A` above left carry clear -- 252 has no bottom bit -- and the generator reads
          // it.
          roll = _rng.Next(false);
          _draw.y1 = roll.value;
          _dust.y[at] = roll.value;
        }
      }
      else
      {
        _draw.zz = _dust.z[at];
      }

      (void)PlotRelativePixel(_canvas, _draw);
    }
  }

  namespace
  {

    /*
     * 6502: ST2 -- flip the roll and the pitch, and recompute both complements.
     *
     * `STARS2` runs this before the loop and again after it, so a whole call leaves the angles as it
     * found them. In between, the left view and the right view are the same arithmetic: one of them
     * runs it with the signs turned over. `RAT` is the sign, and it is zero for the left view, which
     * is why that one is the unflipped case.
     *
     * It also NORMALISES: `ALP2+1` and `BET2+1` come out as the exact complements of `ALP2` and
     * `BET2` whether or not they went in that way.
     */
    void FlipRollAndPitch(FlightState& _flight) noexcept
    {
      _flight.alpha = static_cast<std::uint8_t>(_flight.alpha ^ _flight.rat);
      _flight.alp2 = static_cast<std::uint8_t>(_flight.alp2 ^ _flight.rat);
      _flight.alp2Next = static_cast<std::uint8_t>(_flight.alp2 ^ 0x80u);
      _flight.bet2 = static_cast<std::uint8_t>(_flight.bet2 ^ _flight.rat);
      _flight.bet2Next = static_cast<std::uint8_t>(_flight.bet2 ^ 0x80u);
    }

  } // namespace

  void MoveStardustSideways(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, FlightState& _flight, Stardust& _dust, Rng& _rng,
                            std::uint8_t _view) noexcept
  {
    /*
     * 6502: LDA #0 / CPX #2 / ROR A / STA RAT / EOR #%10000000 / STA RAT2.
     *
     * X is the view ALREADY DECREMENTED -- `STARS` reaches here through a `DEX` -- so the
     * comparison is against the left view (2) rather than against 2 as a view number, and the
     * carry it leaves is set for the right view and clear for the left.
     */
    const std::uint8_t index = static_cast<std::uint8_t>(_view - 1u);
    _flight.rat = (index >= 2u) ? 0x80u : 0x00u;
    _flight.rat2 = static_cast<std::uint8_t>(_flight.rat ^ 0x80u);

    FlipRollAndPitch(_flight);

    for (std::uint8_t at = _dust.count; at != 0u; --at)
    {
      /*
       * 6502: STL2 -- and the first thing to notice is what is NOT here. The dust does not come
       * closer or recede: `SZ` is untouched from one frame to the next, and only a speck that is
       * replaced ever gets a new distance. Sideways, everything slides across at a rate set by how
       * far away it is, and nothing else.
       */
      _draw.zz = _dust.z[at];
      (void)DivideSpeedBy(_math, _flight, static_cast<std::uint8_t>(_dust.z[at] >> 3));
      _dust.newzp = _math.p;
      _math.s = static_cast<std::uint8_t>(_math.p ^ _flight.rat2);

      _math.p = _dust.xLow[at];
      _draw.x1 = _dust.x[at];
      AddSignedResult sum = AddSigned(_math, _draw.x1);
      _math.s = sum.high;
      _math.r = sum.low;

      // The pitch, twice: once into the x it has just stepped and once into the y.
      _draw.y1 = _dust.y[at];
      std::uint8_t pitched = MultiplyScaledBy(_math, _flight.bet1, static_cast<std::uint8_t>(_draw.y1 ^ _flight.bet2));
      sum = AddSigned(_math, pitched);
      _math.xx = sum.low;
      _math.xxNext = sum.high;

      _math.r = _dust.yLow[at];
      _math.s = _draw.y1;
      pitched = MultiplyScaledBy(_math, _flight.bet1, static_cast<std::uint8_t>(sum.high ^ _flight.bet2Next));
      sum = AddSigned(_math, pitched);
      _math.yy = sum.low;
      _math.yyNext = sum.high;

      // And the roll, as one scale factor used by both multiply-accumulates.
      _math.q = MultiplyScaledBy(_math, _flight.alp1, static_cast<std::uint8_t>(sum.high ^ _flight.alp2));

      _math.r = _math.xx;
      _math.s = _math.xxNext;
      sum = MultiplyAndAdd(_math, static_cast<std::uint8_t>(_math.xxNext ^ 0x80u));
      _math.xxNext = sum.high;
      _dust.xLow[at] = sum.low;

      _math.r = _math.yy;
      _math.s = _math.yyNext;
      sum = MultiplyAndAdd(_math, _math.yyNext);
      _math.s = sum.high;
      _math.r = sum.low;
      _math.p = 0;
      PlotStardust(_canvas, _draw, _math, _dust, at, _flight.alpha);

      _dust.x[at] = _math.xxNext;
      _draw.x1 = _math.xxNext;

      /*
       * 6502: AND #%01111111 / EOR #%01111111 / CMP newzp / BCC KILL2 / BEQ KILL2.
       *
       * The `EOR` after the `AND` is 127 minus the magnitude, so what is being compared is how much
       * ROOM the speck has left against how far it moves in a frame -- a speck that would step off
       * the side next frame is replaced this one. `newzp` is the step, which is why it had to be
       * kept: nothing else in the loop still holds it by the time the test runs.
       *
       * Two branches, two entry carries: `BCC` arrives with it clear and `BEQ` with it set, and the
       * generator reads it.
       */
      const std::uint8_t room = static_cast<std::uint8_t>((_math.xxNext & 0x7Fu) ^ 0x7Fu);

      bool killed = room <= _dust.newzp;
      bool entryCarry = room == _dust.newzp;
      bool atSide = killed;

      if (!killed)
      {
        _dust.y[at] = _math.yyNext;
        _draw.y1 = _math.yyNext;

        // 6502: CMP #116 / BCS ST5 -- and no test on the distance at all, because it has not
        // changed.
        if ((_math.yyNext & 0x7Fu) >= 116u)
        {
          killed = true;
          entryCarry = true;
          atSide = false;
        }
      }

      if (killed)
      {
        RngResult roll = _rng.Next(entryCarry);

        if (atSide)
        {
          // 6502: KILL2 -- back in at the edge the dust is coming FROM, which is the side `RAT`
          // names, at any height.
          _draw.y1 = roll.value;
          _dust.y[at] = roll.value;
          _draw.x1 = static_cast<std::uint8_t>(115u | _flight.rat);
          _dust.x[at] = _draw.x1;
        }
        else
        {
          // 6502: ST5 -- or at the top or the bottom, anywhere across. The edge is chosen by the
          // roll's sign, so dust replaced while you are rolling comes in on the side it left.
          _draw.x1 = roll.value;
          _dust.x[at] = roll.value;
          _draw.y1 = static_cast<std::uint8_t>(110u | _flight.alp2Next);
          _dust.y[at] = _draw.y1;
        }

        // 6502: STF1 -- and a distance, which both paths share. The `ORA #8` keeps it off the
        // player's face.
        roll = _rng.Next(roll.carry);
        _draw.zz = static_cast<std::uint8_t>(roll.value | 8u);
        _dust.z[at] = _draw.zz;
      }

      (void)PlotRelativePixel(_canvas, _draw);
    }

    // 6502: the loop leaves through `BEQ ST2`, so the angles are put back on the way out.
    FlipRollAndPitch(_flight);
  }

  void MoveStardust(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, FlightState& _flight, Stardust& _dust, Rng& _rng,
                    std::uint8_t _view) noexcept
  {
    // 6502: STARS -- LDX VIEW / BEQ STARS1 / DEX / BNE ST11 / JMP STARS6 / .ST11 JMP STARS2.
    if (_view == 0u)
    {
      MoveStardustAhead(_canvas, _draw, _math, _flight, _dust, _rng);
    }
    else if (_view == 1u)
    {
      MoveStardustAstern(_canvas, _draw, _math, _flight, _dust, _rng);
    }
    else
    {
      MoveStardustSideways(_canvas, _draw, _math, _flight, _dust, _rng, _view);
    }
  }

} // namespace Elite
