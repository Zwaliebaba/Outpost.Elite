#include "pch.h"

#include "Stardust.h"

#include "EliteTypes.h"
#include "Lines2x.h"

namespace Elite
{

  /*
   * The wrappers.
   *
   * Every one of these is a `LDA` or two and then a fall-through into a routine the port already
   * has. They are named separately because that is what they are in the original -- each has its
   * own include file and its own entry point, and the movers below `JSR` to them by name.
   */

  ScaledDivision DivideSpeedBy(const FlightState& _flight, std::uint8_t _divisor) noexcept
  {
    // 6502: STA Q, then DVID4. Its exit carry is the divide's saturation flag and only `SPS2`
    // reads it -- the stardust follows this with `LSR P`, which makes its own (§6.60).
    return DivideAndScale(_flight.delta, _divisor);
  }

  ScaledDivision DivideSpeedByDistance(const FlightState& _flight, const Stardust& _dust, std::uint8_t _at) noexcept
  {
    return DivideSpeedBy(_flight, _dust.z[_at]);
  }

  Product MultiplyByHeight(const Stardust& _dust, std::uint8_t _at, std::uint8_t _multiplier) noexcept
  {
    // 6502: LDA SY,Y / STA Y1 -- and the mover reads the same byte back as the height's high half.
    return MultiplyMagnitude(_dust.y[_at], _multiplier);
  }

  Product MultiplyByRoll(const FlightState& _flight, std::uint8_t _value) noexcept
  {
    // 6502: LDX ALP1, then MULTS-2's `STX P` and MULTS.
    return MultiplyScaled(_flight.alp1, _value);
  }

  std::uint8_t PlotStardust(Canvas& _canvas, Stardust& _dust, std::uint8_t _at, SignMag16 _value, SignMag16 _addend, std::uint8_t _across,
                            std::uint8_t _down, std::uint8_t _distance, Picture* _picture, std::uint8_t _acrossLow,
                            std::uint8_t _downLow) noexcept
  {
    const AddSignedResult sum = AddSigned(_value, _addend);
    _dust.yLow[_at] = sum.low; // 6502: STX SYL,Y
    (void)PlotRelativePixel(_canvas, _across, _down, _distance);
    if (_picture != nullptr)
    {
      PlotRelativePixel2x(*_picture, _across, _down, _acrossLow, _downLow, _distance);
    }
    return sum.high; // 6502: STA YY+1
  }

  void FlipStardust(Canvas& _canvas, Stardust& _dust, Picture* _picture) noexcept
  {
    for (std::uint8_t at = _dust.count; at != 0u; --at)
    {
      // 6502: LDA SY,Y / STA Y1 ... LDA SX,Y / STA Y1 / STA SY,Y / ... STA X1 / STA SX,Y / LDA SZ,Y / STA ZZ.
      const std::uint8_t was = _dust.y[at];
      _dust.y[at] = _dust.x[at];
      _dust.x[at] = was;

      (void)PlotRelativePixel(_canvas, was, _dust.y[at], _dust.z[at]);
      if (_picture != nullptr)
      {
        // The fractions are NOT swapped by `FLIP`, so the twin's half-pixel goes on the axis the
        // byte now belongs to rather than the one it came from -- which is what the next frame's
        // erase will read, so the pair still holds (Resolution.md section 13).
        PlotRelativePixel2x(*_picture, was, _dust.y[at], _dust.xLow[at], _dust.yLow[at], _dust.z[at]);
      }
    }
  }

  namespace
  {

    /// 6502: the `LSR P / ROR A` pair the front and rear views open with -- two shifts of a
    /// sixteen-bit value held across P and A, which halve the reciprocal of the distance twice.
    /// P is the whole part `DVID4` left and A the fraction it returned; what comes back is the
    /// halved fraction and the last bit out of it, which the front view's subtraction runs on.
    ShiftResult HalveTwice(ScaledDivision _step) noexcept
    {
      std::uint8_t whole = _step.whole;
      ShiftResult rotated{_step.fraction, false};
      for (int shift = 0; shift < 2; ++shift)
      {
        const bool into = (whole & 0x01u) != 0u;
        whole = static_cast<std::uint8_t>(whole >> 1);
        rotated = RotateRight(rotated.value, into);
      }
      return rotated;
    }

    /// 6502: `ASL P / ROL A / STA T / LDA #0 / ROR A / ORA T` -- the front and rear views both end
    /// their pitch step with this, which doubles (A P) and folds the bit that fell off the top back
    /// in as a sign. What comes out is the (A P) the `ADD` after it takes.
    SignMag16 DoubleAndFold(Product _product) noexcept
    {
      const ShiftResult low = RotateLeftValue(_product.low, false);
      const ShiftResult high = RotateLeft(_product.high, low.carry);

      return SignMag16{low.value, static_cast<std::uint8_t>(RotateRight(0, high.carry).value | high.value)};
    }

  } // namespace

  void MoveStardustAhead(Canvas& _canvas, const FlightState& _flight, Stardust& _dust, Rng& _rng, Picture* _picture) noexcept
  {
    for (std::uint8_t at = _dust.count; at != 0u; --at)
    {
      /*
       * `SXL` and `SYL` as the last frame's plot left them, staged beside the position that plot
       * used because both are overwritten before the erase below reaches them. They are the wide
       * mark's half-pixel and nothing faithful reads them (Resolution.md section 4.3).
       */
      const std::uint8_t wasAcrossLow = _dust.xLow[at];
      const std::uint8_t wasDownLow = _dust.yLow[at];

      // 6502: STL1 -- the speed over the distance, halved twice, is how far this speck moves.
      // The `ORA #1` stops a distant speck dividing by zero further down.
      const ShiftResult halved = HalveTwice(DivideSpeedByDistance(_flight, _dust, at));
      const std::uint8_t scale = static_cast<std::uint8_t>(halved.value | 1u); // 6502: ORA #1 / STA Q

      // The speck comes towards you, so its distance falls by four times the speed. The borrow
      // this subtraction runs on is the one the second `ROR A` above left, not a `SEC`.
      const SubResult zLow = SubtractWithCarry(_dust.zLow[at], _flight.delt4, halved.carry);
      _dust.zLow[at] = zLow.value;
      std::uint8_t distance = _dust.z[at]; // 6502: STA ZZ -- the distance it WAS, which is what the erase plots at
      _dust.z[at] = SubtractWithCarry(_dust.z[at], _flight.delt4Next, zLow.carry).value;

      // Its height and its distance across, each scaled by how much closer it now is. `X1` and
      // `Y1` are the speck's old position, staged for the erase (M2-c: locals).
      std::uint8_t y1 = _dust.y[at]; // 6502: MLU1's STA Y1
      const Product height = MultiplyByHeight(_dust, at, scale);
      const AddResult yLow = AddWithCarry(height.low, _dust.yLow[at], height.carry);
      // 6502: STA R ... STA S -- the height is the (S R) the roll's first `ADD` runs on.
      const SignMag16 heightSum{yLow.value, AddWithCarry(y1, height.high, yLow.carry).value}; // 6502: YY(1 0)

      std::uint8_t x1 = _dust.x[at]; // 6502: STA X1
      const Product across = MultiplyMagnitude(x1, scale);
      const AddResult xLow = AddWithCarry(across.low, _dust.xLow[at], across.carry);
      SignMag16 x{xLow.value, AddWithCarry(x1, across.high, xLow.carry).value}; // 6502: XX(1 0)

      // 6502: the roll, as two multiply-and-adds with the signs crossed over.
      Product rolled = MultiplyByRoll(_flight, static_cast<std::uint8_t>(x.hi ^ _flight.alp2Next));
      AddSignedResult sum = AddSigned(rolled.Pair(), heightSum);
      SignMag16 y{sum.low, sum.high}; // 6502: STA YY+1 / STX YY

      // 6502: JSR MLS2 -- (S R) = XX(1 0), then MLS1 and the `ADD`.
      rolled = MultiplyByRoll(_flight, static_cast<std::uint8_t>(sum.high ^ _flight.alp2));
      sum = AddSigned(rolled.Pair(), x);
      x = SignMag16{sum.low, sum.high};

      // And the pitch: 6502: LDX BET1 / ... / JSR MULTS-2 / STA Q / JSR MUT2 -- (S R) = XX(1 0)
      // again, and `MULT1` squares the pitch term, because `STA Q` left it in A (§6.44).
      const std::uint8_t pitch = MultiplyScaled(_flight.bet1, static_cast<std::uint8_t>(y.hi ^ _flight.bet2Next)).high;
      const Product pitched = MultiplySigned(pitch, pitch);
      sum = AddSigned(DoubleAndFold(pitched), x);
      x.hi = sum.high;
      _dust.xLow[at] = sum.low;

      // 6502: LDA YY / STA R / LDA YY+1 / STA S / LDA #0 / STA P / LDA BETA / EOR #128 / JSR PIX1.
      y.hi = PlotStardust(_canvas, _dust, at, SignMag16{0u, static_cast<std::uint8_t>(_flight.beta ^ 0x80u)}, y, x1, y1, distance,
                          _picture, wasAcrossLow, wasDownLow);

      // 6502: the three kill tests. A speck that has drifted more than 120 either way, or come
      // closer than 16, is not clipped -- it is thrown away and a new one rolled at the edge.
      x1 = x.hi;
      _dust.x[at] = x.hi;

      /*
       * Three kill tests, and WHICH of them fires decides the carry the first `DORND` below runs
       * on: the two `CMP #120 / BCS` tests arrive with it set and the `CMP #16 / BCC` with it
       * clear. The generator takes the carry as an operand, so the speck that replaces one which
       * drifted sideways is a different speck from the one replacing a speck that came too close.
       */
      bool killed = (x.hi & 0x7Fu) >= 120u;
      bool entryCarry = true;

      if (!killed)
      {
        _dust.y[at] = y.hi;
        y1 = y.hi;

        killed = (y.hi & 0x7Fu) >= 120u;
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
        y1 = static_cast<std::uint8_t>(roll.value | 4u);
        _dust.y[at] = y1;

        roll = _rng.Next(roll.carry);
        x1 = static_cast<std::uint8_t>(roll.value | 8u);
        _dust.x[at] = x1;

        roll = _rng.Next(roll.carry);
        distance = static_cast<std::uint8_t>(roll.value | 144u);
        _dust.z[at] = distance;
      }
      else
      {
        distance = _dust.z[at];
      }

      (void)PlotRelativePixel(_canvas, x1, y1, distance);
      if (_picture != nullptr)
      {
        // The fractions the loop has just stored, which are this speck's own: the mark is drawn at
        // the position the two bytes name, and next frame's erase reads the same two.
        PlotRelativePixel2x(*_picture, x1, y1, _dust.xLow[at], _dust.yLow[at], distance);
      }
    }
  }

  void MoveStardustAstern(Canvas& _canvas, const FlightState& _flight, Stardust& _dust, Rng& _rng, Picture* _picture) noexcept
  {
    for (std::uint8_t at = _dust.count; at != 0u; --at)
    {
      /*
       * `SXL` and `SYL` as the last frame's plot left them, staged beside the position that plot
       * used because both are overwritten before the erase below reaches them. They are the wide
       * mark's half-pixel and nothing faithful reads them (Resolution.md section 4.3).
       */
      const std::uint8_t wasAcrossLow = _dust.xLow[at];
      const std::uint8_t wasDownLow = _dust.yLow[at];

      // 6502: STL6 -- the same opening as the front view, down to the `ORA #1`. The carry the
      // second `ROR A` leaves is not read: the front view's next instruction is an `SBC`, and this
      // one's is a `JSR`.
      const std::uint8_t scale = static_cast<std::uint8_t>(HalveTwice(DivideSpeedByDistance(_flight, _dust, at)).value | 1u);

      /*
       * Looking backwards the dust recedes, so every step the front view adds this one subtracts
       * and the distance goes UP rather than down. It is not the same routine with a sign, though:
       * the coordinates are done in the other order, the roll's two sign bytes are swapped, the
       * pitch is not negated, and both kill tests are different. Two routines, deliberately.
       */
      std::uint8_t x1 = _dust.x[at]; // 6502: STA X1 -- the old position, staged for the erase (M2-c: locals)
      const Product across = MultiplyMagnitude(x1, scale);
      const SubResult xLow = SubtractWithCarry(_dust.xLow[at], across.low, across.carry);
      const SubResult xHigh = SubtractWithCarry(x1, across.high, xLow.carry);
      SignMag16 x{xLow.value, xHigh.value}; // 6502: XX(1 0)

      std::uint8_t y1 = _dust.y[at]; // 6502: MLU1's STA Y1
      const Product height = MultiplyByHeight(_dust, at, scale);
      const SubResult yLow = SubtractWithCarry(_dust.yLow[at], height.low, height.carry);
      const SubResult yHigh = SubtractWithCarry(y1, height.high, yLow.carry);
      // 6502: STA R ... STA S -- the height is the (S R) the roll's first `ADD` runs on.
      const SignMag16 heightSum{yLow.value, yHigh.value}; // 6502: YY(1 0)

      // 6502: `ADC DELT4` -- on the borrow the subtraction above left, with no `CLC` between them.
      const AddResult zLow = AddWithCarry(_dust.zLow[at], _flight.delt4, yHigh.carry);
      _dust.zLow[at] = zLow.value;
      std::uint8_t distance = _dust.z[at]; // 6502: STA ZZ
      _dust.z[at] = AddWithCarry(_dust.z[at], _flight.delt4Next, zLow.carry).value;

      // The roll, and the two sign bytes are the other way round from the front view's -- which is
      // the whole of what makes the dust roll the opposite way when you look behind you.
      Product rolled = MultiplyByRoll(_flight, static_cast<std::uint8_t>(x.hi ^ _flight.alp2));
      AddSignedResult sum = AddSigned(rolled.Pair(), heightSum);
      SignMag16 y{sum.low, sum.high};

      // 6502: JSR MLS2 -- (S R) = XX(1 0), then MLS1 and the `ADD`.
      rolled = MultiplyByRoll(_flight, static_cast<std::uint8_t>(sum.high ^ _flight.alp2Next));
      sum = AddSigned(rolled.Pair(), x);
      x = SignMag16{sum.low, sum.high};

      /*
       * And the pitch, where the two routines diverge further than a sign: `STARS1` squares the
       * pitch term (`STA Q / JSR MUT2` leaves A holding what it just stored), and this one
       * multiplies it by the negated x instead -- 6502: STA Q / LDA XX+1 / STA S / EOR #128 /
       * JSR MUT1, which is (S R) = XX(1 0) and then `MULT1`. The port keeps both as written
       * (ADR-003).
       */
      const std::uint8_t pitch = MultiplyScaled(_flight.bet1, static_cast<std::uint8_t>(y.hi ^ _flight.bet2Next)).high;
      const Product pitched = MultiplySigned(static_cast<std::uint8_t>(x.hi ^ 0x80u), pitch);
      sum = AddSigned(DoubleAndFold(pitched), x);
      x.hi = sum.high;
      _dust.xLow[at] = sum.low;

      // 6502: LDA YY / STA R / LDA YY+1 / STA S / LDA #0 / STA P / LDA BETA / JSR PIX1.
      y.hi = PlotStardust(_canvas, _dust, at, SignMag16{0u, _flight.beta}, y, x1, y1, distance, _picture, wasAcrossLow, wasDownLow);

      x1 = x.hi;
      _dust.x[at] = x.hi;
      y1 = y.hi;
      _dust.y[at] = y.hi;

      /*
       * Two kill tests rather than three, and both on the values just stored: more than 110 up or
       * down, or further away than 160. There is no test on x at all -- a speck can drift as far
       * sideways as the arithmetic takes it, because the projection has already wrapped it.
       *
       * Both arrive at the generator with carry SET, so `KILL6` has one entry where `KILL1` has
       * two.
       */
      const bool killed = (y.hi & 0x7Fu) >= 110u || _dust.z[at] >= 160u;

      if (killed)
      {
        // 6502: KILL6 -- a new distance first, between 10 and 137, and its own bottom bits then
        // choose which edge the speck comes back in at. Free randomness the generator is not
        // called a fourth time for.
        RngResult roll = _rng.Next(true);
        const AddResult renewed = AddWithCarry(static_cast<std::uint8_t>(roll.value & 0x7Fu), 10u, roll.carry);
        _dust.z[at] = renewed.value;
        distance = renewed.value;

        if (RotateRight(renewed.value, false).carry)
        {
          // 6502: ST4 -- anywhere across, at the top or the bottom.
          roll = _rng.Next(true);
          x1 = roll.value;
          _dust.x[at] = roll.value;

          y1 = RotateRight(230u, RotateRight(roll.value, false).carry).value;
          _dust.y[at] = y1;
        }
        else
        {
          // 6502: at the left or the right edge, anywhere up or down.
          const bool side = RotateRight(static_cast<std::uint8_t>(renewed.value >> 1), false).carry;
          x1 = RotateRight(252u, side).value;
          _dust.x[at] = x1;

          // The `ROR A` above left carry clear -- 252 has no bottom bit -- and the generator reads
          // it.
          roll = _rng.Next(false);
          y1 = roll.value;
          _dust.y[at] = roll.value;
        }
      }
      else
      {
        distance = _dust.z[at];
      }

      (void)PlotRelativePixel(_canvas, x1, y1, distance);
      if (_picture != nullptr)
      {
        // The fractions the loop has just stored, which are this speck's own: the mark is drawn at
        // the position the two bytes name, and next frame's erase reads the same two.
        PlotRelativePixel2x(*_picture, x1, y1, _dust.xLow[at], _dust.yLow[at], distance);
      }
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

  void MoveStardustSideways(Canvas& _canvas, FlightState& _flight, Stardust& _dust, Rng& _rng, std::uint8_t _view,
                            Picture* _picture) noexcept
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
       * `SXL` and `SYL` as the last frame's plot left them, staged beside the position that plot
       * used because both are overwritten before the erase below reaches them. They are the wide
       * mark's half-pixel and nothing faithful reads them (Resolution.md section 4.3).
       */
      const std::uint8_t wasAcrossLow = _dust.xLow[at];
      const std::uint8_t wasDownLow = _dust.yLow[at];

      /*
       * 6502: STL2 -- and the first thing to notice is what is NOT here. The dust does not come
       * closer or recede: `SZ` is untouched from one frame to the next, and only a speck that is
       * replaced ever gets a new distance. Sideways, everything slides across at a rate set by how
       * far away it is, and nothing else.
       */
      std::uint8_t distance = _dust.z[at]; // 6502: STA ZZ
      const ScaledDivision step = DivideSpeedBy(_flight, static_cast<std::uint8_t>(_dust.z[at] >> 3)); // 6502: JSR DV41
      _dust.keptQuotient = step.whole;                                                                          // 6502: LDA P / STA newzp

      // 6502: EOR RAT2 / STA S -- (S R) is the step with the view's sign over it, R being the
      // fraction `DVID4` left; and (A P) is the particle's x.
      const SignMag16 sideways{step.fraction, static_cast<std::uint8_t>(step.whole ^ _flight.rat2)};
      std::uint8_t x1 = _dust.x[at]; // 6502: STA X1 -- the old position, staged for the erase (M2-c: locals)
      AddSignedResult sum = AddSigned(SignMag16{_dust.xLow[at], x1}, sideways);
      const SignMag16 stepped = sum.Pair(); // 6502: STA S / STX R

      // The pitch, twice: once into the x it has just stepped and once into the y.
      std::uint8_t y1 = _dust.y[at]; // 6502: STA Y1
      Product pitched = MultiplyScaled(_flight.bet1, static_cast<std::uint8_t>(y1 ^ _flight.bet2)); // 6502: JSR MULTS-2
      sum = AddSigned(pitched.Pair(), stepped);
      SignMag16 x{sum.low, sum.high}; // 6502: XX(1 0)

      pitched = MultiplyScaled(_flight.bet1, static_cast<std::uint8_t>(sum.high ^ _flight.bet2Next));
      sum = AddSigned(pitched.Pair(), SignMag16{_dust.yLow[at], y1});
      SignMag16 y{sum.low, sum.high}; // 6502: YY(1 0)

      // And the roll, as one scale factor used by both multiply-accumulates. 6502: STA Q.
      const std::uint8_t roll = MultiplyScaled(_flight.alp1, static_cast<std::uint8_t>(sum.high ^ _flight.alp2)).high;

      sum = MultiplyAndAdd(static_cast<std::uint8_t>(x.hi ^ 0x80u), roll, x);
      x.hi = sum.high;
      _dust.xLow[at] = sum.low;

      sum = MultiplyAndAdd(y.hi, roll, y);
      // 6502: STA S / STX R / LDA #0 / STA P / LDA ALPHA / JSR PIX1.
      y.hi = PlotStardust(_canvas, _dust, at, SignMag16{0u, _flight.alpha}, sum.Pair(), x1, y1, distance, _picture, wasAcrossLow,
                          wasDownLow);

      _dust.x[at] = x.hi;
      x1 = x.hi;

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
      const std::uint8_t room = static_cast<std::uint8_t>((x.hi & 0x7Fu) ^ 0x7Fu);

      bool killed = room <= _dust.keptQuotient;
      bool entryCarry = room == _dust.keptQuotient;
      bool atSide = killed;

      if (!killed)
      {
        _dust.y[at] = y.hi;
        y1 = y.hi;

        // 6502: CMP #116 / BCS ST5 -- and no test on the distance at all, because it has not
        // changed.
        if ((y.hi & 0x7Fu) >= 116u)
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
          y1 = roll.value;
          _dust.y[at] = roll.value;
          x1 = static_cast<std::uint8_t>(115u | _flight.rat);
          _dust.x[at] = x1;
        }
        else
        {
          // 6502: ST5 -- or at the top or the bottom, anywhere across. The edge is chosen by the
          // roll's sign, so dust replaced while you are rolling comes in on the side it left.
          x1 = roll.value;
          _dust.x[at] = roll.value;
          y1 = static_cast<std::uint8_t>(110u | _flight.alp2Next);
          _dust.y[at] = y1;
        }

        // 6502: STF1 -- and a distance, which both paths share. The `ORA #8` keeps it off the
        // player's face.
        roll = _rng.Next(roll.carry);
        distance = static_cast<std::uint8_t>(roll.value | 8u);
        _dust.z[at] = distance;
      }

      (void)PlotRelativePixel(_canvas, x1, y1, distance);
      if (_picture != nullptr)
      {
        // The fractions the loop has just stored, which are this speck's own: the mark is drawn at
        // the position the two bytes name, and next frame's erase reads the same two.
        PlotRelativePixel2x(*_picture, x1, y1, _dust.xLow[at], _dust.yLow[at], distance);
      }
    }

    // 6502: the loop leaves through `BEQ ST2`, so the angles are put back on the way out.
    FlipRollAndPitch(_flight);
  }

  void MoveStardust(Canvas& _canvas, FlightState& _flight, Stardust& _dust, Rng& _rng, std::uint8_t _view, Picture* _picture) noexcept
  {
    // 6502: STARS -- LDX VIEW / BEQ STARS1 / DEX / BNE ST11 / JMP STARS6 / .ST11 JMP STARS2.
    if (_view == 0u)
    {
      MoveStardustAhead(_canvas, _flight, _dust, _rng, _picture);
    }
    else if (_view == 1u)
    {
      MoveStardustAstern(_canvas, _flight, _dust, _rng, _picture);
    }
    else
    {
      MoveStardustSideways(_canvas, _flight, _dust, _rng, _view, _picture);
    }
  }

} // namespace Elite
