#include "pch.h"

#include "ShipDraw.h"

#include "EliteTypes.h"
#include "Explosion.h"
#include "PlanetDraw.h"
#include "ShipBlueprint.h"
#include "Universe.h"

#include <array>
#include <utility>

namespace Elite
{
  /// 6502: what byte 1 of a fresh cloud's heap starts at, the counter `DOEXP` ages.
  inline constexpr std::uint8_t EXPLOSION_COUNTER_START = 18;

  namespace
  {
    /// 6502: DVL6 / DV9 -- the denominator's top byte, shifted up (at least once, with the bytes
    /// below it) until its top bit is set: what `DVID3B` stores in `Q` and leaves there.
    [[nodiscard]] std::uint8_t ScaledDivisorTop(SignMag24 _divisor) noexcept
    {
      std::uint8_t low = _divisor.lo;
      std::uint8_t middle = _divisor.hi;
      std::uint8_t top = static_cast<std::uint8_t>(_divisor.sgn & 0x7Fu);
      do
      {
        const ShiftResult first = RotateLeftValue(low, false);
        low = first.value;
        const ShiftResult second = RotateLeftValue(middle, first.carry);
        middle = second.value;
        top = RotateLeftValue(top, second.carry).value;
      } while ((top & 0x80u) == 0u);
      return top;
    }
  } // namespace

  KBlock DivideByShipZ(const Ship& _ship, MathWorkspace& _math, SignMag24 _numerator) noexcept
  {
    // Forcing bit 0 of the low byte is what makes the divide below safe, and it is deliberate
    // rather than defensive: a ship exactly on the plane of the screen has z_lo = 0, and the
    // difference between dividing by zero and dividing by one is invisible at this scale.
    const SignMag24 distance{static_cast<std::uint8_t>(_ship.z.lo | 0x01u), _ship.z.hi, _ship.z.sgn};

    // 6502: DV9 -- the divide leaves the scaled divisor in `Q`, and for a ship drawn as a
    // dot that is the frame's Q the altitude check reads (`EndFlightFrame`). The kernel keeps its
    // scratch since M2-b; this byte is recomputed here for that one reader, and `K` is the block
    // this routine returns since M2-c-3.
    _math.lastDivisor = ScaledDivisorTop(distance);
    return DivideSigned24(_numerator, distance);
  }

  ScreenOffset DivideToScreenOffset(const Ship& _ship, MathWorkspace& _math, SignMag24 _numerator) noexcept
  {
    const KBlock quotient = DivideByShipZ(_ship, _math, _numerator);

    // The top two bytes of the quotient, sign removed. Anything at all up there is already past
    // 65,536 and there is nothing to say about where it would be on a 256-pixel view.
    const std::uint8_t top = static_cast<std::uint8_t>((quotient.top & 0x7Fu) | quotient.high);
    if (top != 0u)
    {
      // 6502: PL21 -- SEC and return. X is not touched on this path.
      return ScreenOffset{quotient.low, 0, top, true};
    }

    const std::uint8_t high = quotient.mid;
    if (high >= 4u)
    {
      // 6502: the CPX that overflows at 1024, returning through PL6's RTS with the carry the
      // comparison set. A is still the zero the test above left, which is why `SHPPT` -- which
      // reads A and not the carry -- can miss this and does.
      return ScreenOffset{quotient.low, high, 0, true};
    }

    if ((quotient.top & 0x80u) == 0u)
    {
      // Positive, and the carry is already clear: the comparison above did not set it.
      return ScreenOffset{quotient.low, high, 0, false};
    }

    // 6502: the two's complement negation. The added one runs with the carry the comparison left
    // CLEAR, so it adds exactly one -- the one place in this file where the incoming carry is not
    // part of the sum, and the only one where reading it as `+ 1 + C` would still be right. The
    // original writes
    // the negated low byte back into `K`; nothing reads it there, and it is the returned value.
    const AddResult low = AddWithCarry(static_cast<std::uint8_t>(quotient.low ^ 0xFFu), 1, false);
    const AddResult negated = AddWithCarry(static_cast<std::uint8_t>(high ^ 0xFFu), 0, low.carry);

    // 6502: PL44 -- the carry cleared, then PL6's return.
    return ScreenOffset{low.value, negated.value, negated.value, false};
  }

  ProjectResult Project(const Ship& _ship, MathWorkspace& _math, Projection& _screen) noexcept
  {
    // 6502: PLS6 on the x coordinate.
    const ScreenOffset across = DivideToScreenOffset(_ship, _math, _ship.x);
    if (across.overflow)
    {
      // 6502: PL2-1, which is PROJ's own return one byte before the next routine begins.
      return ProjectResult{true, across.a};
    }

    // The carry is clear here, so the addition is the plain one it looks like.
    const AddResult x = AddWithCarry(across.low, SPACE_VIEW_CENTRE_X, false);
    _screen.x = x.value;
    const AddResult x1 = AddWithCarry(across.high, 0, x.carry);
    _screen.x1 = x1.value;

    // 6502: the y coordinate, with its sign flipped: up the screen is down the axis.
    const std::uint8_t upwards = static_cast<std::uint8_t>(_ship.y.sgn ^ 0x80u);
    const ScreenOffset down = DivideToScreenOffset(_ship, _math, SignMag24{_ship.y.lo, _ship.y.hi, upwards});
    if (down.overflow)
    {
      return ProjectResult{true, down.a};
    }

    const AddResult y = AddWithCarry(down.low, SPACE_VIEW_CENTRE_Y, false);
    _screen.y = y.value;
    const AddResult y1 = AddWithCarry(down.high, 0, y.carry);
    _screen.y1 = y1.value;

    return ProjectResult{false, y1.value};
  }

  namespace
  {

    /*
     * 6502: Shpt -- one four-pixel horizontal line, written as the line-heap entry that ENDS at
     * offset `_y`. The original walks Y forwards and then back, which is why the entry's four bytes
     * come out at `_y - 1` to `_y + 2` rather than starting where Y is.
     *
     * Returns false where the original branches to `nono-2`. That target pulls two bytes off the
     * stack above `nono`
     * which throws away this routine's own return address, so the failure does not come back here --
     * it returns to SHPPT's caller with the ship marked as not drawn. A bool and an early return say
     * the same thing without needing a stack.
     */
    bool StorePoint(LineHeap& _heap, HeapOffset _run, const Projection& _screen, std::uint8_t _y, std::uint8_t _a) noexcept
    {
      _heap.Write(_run.Byte(_y), _a);
      _heap.Write(_run.Byte(static_cast<std::uint16_t>(_y + 2u)), _a);
      _heap.Write(_run.Byte(static_cast<std::uint16_t>(_y + 1u)), _screen.x);

      // The carry is clear here on both calls: on the first because the `CMP` that let us past the
      // bottom-of-screen test left it clear, and on the second because the first call would have
      // bailed out if its own `ADC` had carried.
      const AddResult right = AddWithCarry(_screen.x, 3, false);
      if (right.carry)
      {
        return false;
      }

      _heap.Write(_run.Byte(static_cast<std::uint16_t>(_y - 1u)), right.value);
      return true;
    }

  } // namespace

  void DrawShipLines(Canvas& _canvas, const LineHeap& _heap, HeapOffset _run, Picture* _picture) noexcept
  {
    // Resolution.md §4, rule T3: the same run on the wide surface, from the SAME heap bytes, so the
    // erase below matches what was drawn without a second record to keep in step. Nothing attached
    // is a fixture comparing the canvas, which is most of them.
    if (_picture != nullptr)
    {
      DrawShipLines2x(*_picture, _heap, _run);
    }

    const std::uint8_t length = _heap.Read(_run);
    if (length < 4u)
    {
      return;
    }

    // A byte, and the comparison is on a byte, because the original's index and limit are bytes.
    // A heap longer than 253 bytes would wrap Y and loop forever here exactly as it does there;
    // the length comes from byte 5 of a blueprint, and the largest of the thirty-three is 157.
    //
    // The test being at the BOTTOM is the instruction order and not a behaviour: the guard above
    // has already established that the first one would pass. Same shape as `DVL6` in `DVID3B`, and
    // a while-loop here is an equivalent mutation for the same reason.
    std::uint8_t y = 1;
    do
    {
      Line line;
      line.x1 = _heap.Read(_run.Byte(y));
      line.y1 = _heap.Read(_run.Byte(static_cast<std::uint16_t>(y + 1u)));
      line.x2 = _heap.Read(_run.Byte(static_cast<std::uint16_t>(y + 2u)));
      line.y2 = _heap.Read(_run.Byte(static_cast<std::uint16_t>(y + 3u)));

      (void)DrawLine(_canvas, line);

      y = static_cast<std::uint8_t>(y + 4);
    } while (y < length);
  }

  void StoreLineCountAndDraw(Canvas& _canvas, LineHeap& _heap, HeapOffset _run, std::uint8_t _count, Picture* _picture) noexcept
  {
    _heap.Write(_run, _count);
    DrawShipLines(_canvas, _heap, _run, _picture);
  }

  bool EraseShip(Canvas& _canvas, Ship& _ship, const LineHeap& _heap, bool _carryIn, Picture* _picture) noexcept
  {
    if (!Has(_ship.state, ShipStateBit::OnScreen))
    {
      return _carryIn; // 6502: LL10-1 -- a bare return, and the flag is the caller's
    }

    _ship.state = static_cast<std::uint8_t>(_ship.state ^ Mask(ShipStateBit::OnScreen));
    DrawShipLines(_canvas, _heap, _ship.heap, _picture);

    // 6502: LL155's exit -- the test against 4 clears it for a heap with no line on it, and the
    // comparison that ends the drawing loop leaves it set for every heap that had one.
    return _heap.Read(_ship.heap) >= 4u;
  }

  void SeedExplosionCloud(LineHeap& _heap, HeapOffset _run, std::uint8_t _explosionCount, Rng& _rng, bool _carryIn) noexcept
  {
    _heap.Write(_run.Byte(1u), EXPLOSION_COUNTER_START); // 6502: 18 into byte 1 of the heap
    _heap.Write(_run.Byte(2u), _explosionCount);         // 6502: byte 7 of the blueprint into byte 2

    // 6502: EE55 -- four random bytes into the heap. The first roll takes the carry `EE51` left,
    // and each later one the clear the loop's own comparison leaves while the index is under six.
    bool carry = _carryIn;
    for (std::uint16_t byte = 3u; byte <= 6u; ++byte)
    {
      _heap.Write(_run.Byte(byte), _rng.Next(carry).value);
      carry = false;
    }
  }

  void DrawShipAsPoint(Canvas& _canvas, Ship& _ship, LineHeap& _heap, MathWorkspace& _math, Projection& _screen,
                       Picture* _picture) noexcept
  {
    // The flag `EE51` returns goes nowhere from here: `SHPPT` overwrites it in `PROJ`'s arithmetic.
    static_cast<void>(EraseShip(_canvas, _ship, _heap, false, _picture));

    const ProjectResult projected = Project(_ship, _math, _screen);

    // 6502: the two high bytes ORed together, and anything set goes to nono. See the header -- this
    // is not the carry, and the difference is
    // visible whenever `PLS6` overflows on its second test rather than its first.
    const bool offScreen = (projected.a | _screen.x1) != 0u || _screen.y >= static_cast<std::uint8_t>(SPACE_VIEW_BOTTOM - 2);

    const HeapOffset heap = _ship.heap;

    // The two stores write as they go and can fail half way, which is what the original does: the
    // first four bytes of the entry are already on the heap when the second call gives up. Nothing
    // reads them, because the length byte is only written on the path below.
    if (offScreen || !StorePoint(_heap, heap, _screen, 2, _screen.y) ||
        !StorePoint(_heap, heap, _screen, 6, AddWithCarry(_screen.y, 1, false).value))
    {
      // 6502: nono -- one bit masked out of the state byte. Reached four ways, and all four leave
      // the ship marked as not on the screen.
      _ship.state = Without(_ship.state, ShipStateBit::OnScreen);
      return;
    }

    _ship.state = With(_ship.state, ShipStateBit::OnScreen);

    /*
     * `SHPPT` draws a distant ship as TWO short horizontal lines, one above the other, which is
     * what `StorePoint` has just put on the heap -- and the wide surface draws the same two out of
     * the same bytes when `StoreLineCountAndDraw` below reaches `DrawShipLines2x`. There is nothing
     * for the dot to do here: since RS-3 the twin reads the faithful heap, so a mark that had an
     * arithmetic of its own could not exist.
     *
     * Its position is the faithful one and not a `Project2x`, which RS-3 measured to be the only
     * honest answer: `PROJ` divides through `DVID3B`, whose eight-bit mantissa is out by up to
     * fifteen pixels inside `PLS6`'s own range, so a twin that divided exactly would put a distant
     * ship somewhere else entirely (§13).
     */
    StoreLineCountAndDraw(_canvas, _heap, heap, 8, _picture);
  }

  void DotProducts(Vector16 _vector, GeometryWorkspace& _geometry) noexcept
  {
    // The six bytes of XX15, as the three sign-magnitude pairs the dot product treats them as.
    const std::array<std::uint8_t, 3> magnitude = {_vector.x.lo, _vector.y.lo, _vector.z.lo};
    const std::array<std::uint8_t, 3> sign = {_vector.x.hi, _vector.y.hi, _vector.z.hi};

    // Three vectors of six, and the loop in the original stops at 17, so it runs for
    // X = 0, 6 and 12 and stops at 18 rather than testing a count.
    for (std::size_t vector = 0; vector < 3u; ++vector)
    {
      const std::size_t base = vector * 6u;

      // The first term sets S, which is the sign the whole sum is accumulated against -- `LL38`
      // FLIPS it when a subtraction goes past zero, so what comes out at the end is the sign of the
      // answer and not of the first product (6502: `LL38`).
      SignMag16 total{MultiplyByLog(_geometry.scaledOrientation[base], magnitude[0], false).value,
                      static_cast<std::uint8_t>(sign[0] ^ _geometry.scaledOrientation[base + 1])};

      // 6502: the second product goes straight back over its own multiplier, and `LL38` combines
      // it with the total under the two signs.
      std::uint8_t term = MultiplyByLog(_geometry.scaledOrientation[base + 2], magnitude[1], false).value;
      SignedSum combined = CombineSigned(static_cast<std::uint8_t>(sign[1] ^ _geometry.scaledOrientation[base + 3]), term, total);
      total = SignMag16{combined.value, combined.sign}; // 6502: back through `T` into `R`

      term = MultiplyByLog(_geometry.scaledOrientation[base + 4], magnitude[2], false).value;
      combined = CombineSigned(static_cast<std::uint8_t>(sign[2] ^ _geometry.scaledOrientation[base + 5]), term, total);

      _geometry.dotProducts[vector * 2u] = combined.value;
      _geometry.dotProducts[vector * 2u + 1u] = combined.sign;
    }
  }

  PreparedSlope PrepareSlope(Slope _slope, SignMag16 _distance) noexcept
  {
    PreparedSlope prepared{_distance, _slope.gradient, 0}; // 6502: the gradient into Q

    const std::uint8_t original = _distance.hi; // 6502: S
    if ((original & 0x80u) != 0u)
    {
      // (S R) = -(S R). The low byte is subtracted from zero and the high byte adds nothing on
      // that subtraction's carry, so the two are one sixteen-bit negation and not two eight-bit
      // ones.
      const SubResult low = SubtractWithCarry(0, _distance.lo, true);
      prepared.magnitude.lo = low.value;
      prepared.magnitude.hi = AddWithCarry(static_cast<std::uint8_t>(original ^ 0xFFu), 0, low.carry).value;
    }

    prepared.sign = static_cast<std::uint8_t>(original ^ _slope.direction);
    return prepared;
  }

  namespace
  {

    /// 6502: LL122 -- (Y X) = (S R) * Q, shift-and-add, with the first shift happening BEFORE any
    /// addition so the product comes out halved. That is not an error to correct: the caller wants
    /// the step for half a pixel.
    SlopeStep MultiplySlope(PreparedSlope _prepared) noexcept
    {
      std::uint8_t low = 0;
      std::uint8_t high = 0;

      // One shift of the multiplicand, rippling through both its bytes, and one bit off the top of
      // the multiplier. All three bytes are this routine's own since M2-c-2.
      const auto step = [&_prepared]() noexcept
      {
        const bool intoR = (_prepared.magnitude.hi & 0x01u) != 0u;
        _prepared.magnitude.hi = static_cast<std::uint8_t>(_prepared.magnitude.hi >> 1);
        _prepared.magnitude.lo = RotateRight(_prepared.magnitude.lo, intoR).value;

        const ShiftResult multiplier = RotateLeftValue(_prepared.divisor, false);
        _prepared.divisor = multiplier.value;
        return multiplier.carry;
      };

      /*
       * The first shift is OUTSIDE the loop, and that is the whole shape of the routine rather than
       * a detail: the entry shifts both ends once and branches into `LL126`, which -- being where
       * the "have we run out of multiplier" test lives -- shifts again before testing. So a Q of
       * zero still gets TWO shifts, not one, and the port shifted once until the sweep said
       * otherwise.
       */
      bool add = step();
      for (;;)
      {
        if (add)
        {
          const AddResult sum = AddWithCarry(low, _prepared.magnitude.lo, false);
          low = sum.value;
          high = AddWithCarry(high, _prepared.magnitude.hi, sum.carry).value;
        }

        add = step();
        if (!add && _prepared.divisor == 0u)
        {
          break;
        }
      }

      return SlopeStep{low, high, _prepared.divisor};
    }

    /// 6502: LL121 -- (Y X) = (S R) / Q, restoring division, with (Y X) starting at &FFFE as the bit
    /// counter in the same trick `LL31` uses: the quotient bits push the set bits out of the top.
    SlopeStep DivideSlope(PreparedSlope _prepared) noexcept
    {
      std::uint8_t low = 0xFE;
      std::uint8_t high = 0xFF;

      for (;;)
      {
        const ShiftResult shifted = RotateLeftValue(_prepared.magnitude.lo, false);
        _prepared.magnitude.lo = shifted.value;
        const ShiftResult raised = RotateLeft(_prepared.magnitude.hi, shifted.carry);
        _prepared.magnitude.hi = raised.value;

        bool bit = raised.carry;
        if (raised.carry || _prepared.magnitude.hi >= _prepared.divisor)
        {
          const SubResult difference = SubtractWithCarry(_prepared.magnitude.hi, _prepared.divisor, true);
          _prepared.magnitude.hi = difference.value;
          _prepared.magnitude.lo = SubtractWithCarry(_prepared.magnitude.lo, 0, difference.carry).value;
          bit = true;
        }

        const ShiftResult quotientLow = RotateLeft(low, bit);
        low = quotientLow.value;
        const ShiftResult quotientHigh = RotateLeft(high, quotientLow.carry);
        high = quotientHigh.value;

        if (!quotientHigh.carry)
        {
          break;
        }
      }

      return SlopeStep{low, high, _prepared.divisor};
    }

    /// 6502: LL133 -- negate (Y X). Both loops exit with the carry clear, so the added one is one.
    SlopeStep NegateStep(SlopeStep _step) noexcept
    {
      const AddResult low = AddWithCarry(static_cast<std::uint8_t>(_step.low ^ 0xFFu), 1, false);
      const AddResult high = AddWithCarry(static_cast<std::uint8_t>(_step.high ^ 0xFFu), 0, low.carry);
      return SlopeStep{low.value, high.value, _step.divisorLeft};
    }

    /// The tail both entry points share: run one of the two loops, then take the sign from the byte
    /// `LL129` returned -- negating when it is POSITIVE, because the step has to oppose the slope.
    SlopeStep FinishStep(SlopeStep _step, std::uint8_t _sign) noexcept
    {
      return ((_sign & 0x80u) == 0u) ? NegateStep(_step) : _step;
    }

  } // namespace

  SlopeStep StepAlongX(Slope _slope, std::uint8_t _distanceHigh, std::uint8_t _xLow) noexcept
  {
    // 6502: LL120 -- `R` is overwritten on entry, so whatever the caller left there is dead.
    const PreparedSlope prepared = PrepareSlope(_slope, SignMag16{_xLow, _distanceHigh});
    const SlopeStep step = (_slope.steep != 0u) ? DivideSlope(prepared) : MultiplySlope(prepared);
    return FinishStep(step, prepared.sign);
  }

  SlopeStep StepAlongY(Slope _slope, SignMag16 _distance) noexcept
  {
    const PreparedSlope prepared = PrepareSlope(_slope, _distance);
    const SlopeStep step = (_slope.steep != 0u) ? MultiplySlope(prepared) : DivideSlope(prepared);
    return FinishStep(step, prepared.sign);
  }

  namespace
  {

    /// The move every one of `LL118`'s four clamps ends with: add the sixteen-bit step to the OTHER
    /// coordinate, as a sixteen-bit addition out of the two registers.
    /// `_math` takes the leftover `Q` with it: see `SlopeStep::divisorLeft`.
    void AddStep(SlopeStep _step, std::uint8_t& _low, std::uint8_t& _high, MathWorkspace& _math) noexcept
    {
      _math.lastDivisor = _step.divisorLeft;
      const AddResult sum = AddWithCarry(_step.low, _low, false);
      _low = sum.value;
      _high = AddWithCarry(_step.high, _high, sum.carry).value;
    }

  } // namespace

  void MovePointOnScreen(Point16& _point, Slope _slope, MathWorkspace& _math) noexcept
  {
    // The accumulator threads through the first two clamps: the left-edge branch ends `TAX` with A
    // zero, and the right-edge test below is `BEQ` on that same A. So clamping to the left edge is
    // what stops the right-edge clamp running as well.
    std::uint8_t a = _point.xHigh;

    if ((a & 0x80u) != 0u)
    {
      // x1_hi is negative, so the point is off the LEFT edge. Step to x = 0.
      AddStep(StepAlongX(_slope, a, _point.xLow), _point.yLow, _point.yHigh, _math);
      _point.xLow = 0;
      _point.xHigh = 0;
      a = 0;
    }

    // 6502: LL119 -- x1_hi is non-zero and positive, so the point is off the RIGHT edge. Stepping
    // the high byte down is what makes the step land on 255 rather than 256.
    if (a != 0u)
    {
      AddStep(StepAlongX(_slope, static_cast<std::uint8_t>(a - 1u), _point.xLow), _point.yLow, _point.yHigh, _math);
      _point.xLow = 255;
      _point.xHigh = 0;
    }

    // 6502: LL134 -- y1_hi is negative, so the point is off the TOP. Step to y = 0.
    if ((_point.yHigh & 0x80u) != 0u)
    {
      AddStep(StepAlongY(_slope, SignMag16{_point.yLow, _point.yHigh}), _point.xLow, _point.xHigh, _math);
      _point.yLow = 0;
      _point.yHigh = 0;
    }

    // 6502: LL135 -- and the bottom, which is a subtraction rather than a sign test because the
    // edge is 144 and not zero. R and S hold the difference whether or not the clamp runs, because
    // that difference IS the step's argument.
    const SubResult overshoot = SubtractWithCarry(_point.yLow, SPACE_VIEW_BOTTOM, true);
    const SubResult beyond = SubtractWithCarry(_point.yHigh, 0, overshoot.carry);

    if (!beyond.carry)
    {
      return;
    }

    // 6502: LL139 -- and 143 rather than 144, for the same reason the right edge is 255.
    AddStep(StepAlongY(_slope, SignMag16{overshoot.value, beyond.value}), _point.xLow, _point.xHigh, _math);
    _point.yLow = static_cast<std::uint8_t>(SPACE_VIEW_BOTTOM - 1);
    _point.yHigh = 0;
  }

  namespace
  {

    /// 6502: LL146 -- repack the two clipped points into the four eight-bit coordinates the line
    /// drawing wants. The order matters in the original: `XX15+2` is read into `XX15+1` before it
    /// is overwritten, which is what the four bytes' aliasing makes of it.
    [[nodiscard]] Line RepackClipped(Line16 _line) noexcept
    {
      return Line{_line.first.xLow, _line.first.yLow, _line.second.xLow, _line.second.yLow};
    }

    /// 6502: the four swaps at LLX117 -- exchange the two ends of the line.
    void SwapEnds(Line16& _line) noexcept
    {
      std::swap(_line.first, _line.second);
    }

    /// True when both ends are so far off the same side that no part of the line can be on screen.
    /// 6502: the four sign tests at LL83, which are only reached when neither
    /// end is on the screen. `XX12+2` is the byte the original works in and `LL115` overwrites it
    /// on every path that gets past here; the port keeps writing it until M2-c-3 takes `XX12`.
    bool BothEndsBeyondTheSameEdge(Line16 _line, GeometryWorkspace& _geometry) noexcept
    {
      if (((_line.first.xHigh & _line.second.xHigh) & 0x80u) != 0u)
      {
        return true; // both x high bytes negative -- off the left
      }
      if (((_line.first.yHigh & _line.second.yHigh) & 0x80u) != 0u)
      {
        return true; // both y high bytes negative -- above
      }

      // Both x coordinates past 255: the high bytes minus one are still positive.
      _geometry.dotProducts[2] = static_cast<std::uint8_t>(_line.second.xHigh - 1u);
      const std::uint8_t left = static_cast<std::uint8_t>(_line.first.xHigh - 1u);
      if (((left | _geometry.dotProducts[2]) & 0x80u) == 0u)
      {
        return true;
      }

      // And both below the bottom. The comparison against the view's height is only there for its
      // carry -- the byte it produces is thrown away and the subtraction under it is what gets kept.
      const SubResult firstLow = SubtractWithCarry(_line.first.yLow, SPACE_VIEW_BOTTOM, true);
      _geometry.dotProducts[2] = SubtractWithCarry(_line.first.yHigh, 0, firstLow.carry).value;

      const SubResult secondLow = SubtractWithCarry(_line.second.yLow, SPACE_VIEW_BOTTOM, true);
      const std::uint8_t second = SubtractWithCarry(_line.second.yHigh, 0, secondLow.carry).value;

      return ((second | _geometry.dotProducts[2]) & 0x80u) == 0u;
    }

    /// 6502: LL115 to LL114 -- the line's gradient, scaled so that both differences fit in a byte,
    /// with `T` saying which axis it is measured along.
    [[nodiscard]] Slope MeasureSlope(Line16 _line, GeometryWorkspace& _geometry, MathWorkspace& _math) noexcept
    {
      const SubResult acrossLow = SubtractWithCarry(_line.second.xLow, _line.first.xLow, true);
      _geometry.dotProducts[2] = acrossLow.value;
      const SubResult acrossHigh = SubtractWithCarry(_line.second.xHigh, _line.first.xHigh, acrossLow.carry);
      _geometry.dotProducts[3] = acrossHigh.value;

      const SubResult downLow = SubtractWithCarry(_line.second.yLow, _line.first.yLow, true);
      _geometry.dotProducts[4] = downLow.value;
      const SubResult downHigh = SubtractWithCarry(_line.second.yHigh, _line.first.yHigh, downLow.carry);
      _geometry.dotProducts[5] = downHigh.value;

      // The direction of the slope, which is the two differences' signs EOR'd -- taken now, because
      // both are about to be made positive. 6502: `LL116` stores it in `XX12+3`.
      const std::uint8_t direction = static_cast<std::uint8_t>(downHigh.value ^ _geometry.dotProducts[3]);

      if ((_geometry.dotProducts[5] & 0x80u) != 0u)
      {
        const SubResult low = SubtractWithCarry(0, _geometry.dotProducts[4], true);
        _geometry.dotProducts[4] = low.value;
        _geometry.dotProducts[5] = SubtractWithCarry(0, _geometry.dotProducts[5], low.carry).value;
      }

      // The x difference's high byte is negated into the ACCUMULATOR and never stored back. That
      // looks load-bearing and is not: XX12+3 is not read again between here and `LL116`, which
      // overwrites it with the slope direction from S. Writing the magnitude back is an equivalent
      // mutation and the sweep says so -- which is the only reason this comment is right, because
      // the first version of it claimed the opposite with a plausible argument attached (§6.29).
      std::uint8_t high = _geometry.dotProducts[3];
      if ((high & 0x80u) != 0u)
      {
        const SubResult low = SubtractWithCarry(0, _geometry.dotProducts[2], true);
        _geometry.dotProducts[2] = low.value;
        high = SubtractWithCarry(0, _geometry.dotProducts[3], low.carry).value;
      }

      // 6502: LL111 / LL112 -- halve both until each fits in one byte.
      while (high != 0u || _geometry.dotProducts[5] != 0u)
      {
        const bool intoAcross = (high & 0x01u) != 0u;
        high = static_cast<std::uint8_t>(high >> 1);
        _geometry.dotProducts[2] = RotateRight(_geometry.dotProducts[2], intoAcross).value;

        const bool intoDown = (_geometry.dotProducts[5] & 0x01u) != 0u;
        _geometry.dotProducts[5] = static_cast<std::uint8_t>(_geometry.dotProducts[5] >> 1);
        _geometry.dotProducts[4] = RotateRight(_geometry.dotProducts[4], intoDown).value;
      }

      /*
       * 6502: LL113 -- X is the now-zero high byte, so T starts at zero and the steep branch
       * decrements it to 255.
       *
       * `Q` IS WRITTEN AS WELL AS USED. The divisor stays there, and for a frame whose last ship
       * drew lines it is the byte the altitude check reads as its radicand's low half -- the
       * frame's `Q` (M2-b, §8; risk R22). `R`, the quotient, is the gradient the helpers take as a
       * value since M2-c-2.
       */
      if (_geometry.dotProducts[2] >= _geometry.dotProducts[4])
      {
        _math.lastDivisor = _geometry.dotProducts[2];
        return Slope{DivideByLog(_geometry.dotProducts[4], _math.lastDivisor).value, direction, 0};
      }

      // 6502: LL114 -- steep, so `T` comes out as 255.
      _math.lastDivisor = _geometry.dotProducts[4];
      return Slope{DivideByLog(_geometry.dotProducts[2], _math.lastDivisor).value, direction, 0xFFu};
    }

  } // namespace

  ClipResult ClipLineKeepingSwap(Line16 _line, GeometryWorkspace& _geometry, MathWorkspace& _math, const ClipState& _clip,
                                 std::uint8_t _swapIn, std::uint8_t _secondXHigh) noexcept
  {
    ClipResult result;
    result.swap = _swapIn;

    if ((_clip.clippingOff & 0x80u) != 0u)
    {
      result.line = RepackClipped(_line);
      return result;
    }

    // 6502: LL107 -- is the FAR end on the screen? Both its high bytes zero and its y under 144.
    constexpr std::uint8_t LAST_ROW = static_cast<std::uint8_t>(SPACE_VIEW_BOTTOM - 1);
    std::uint8_t state = LAST_ROW;
    if ((_secondXHigh | _line.second.yHigh) == 0u && LAST_ROW >= _line.second.yLow)
    {
      state = 0;
    }
    result.ends = state;

    // And the near end, by the same test. If both are on screen there is nothing to do; if only
    // the near one is, the state is halved, which is what turns 143 into 71 and clears bit 7.
    if ((_line.first.xHigh | _line.first.yHigh) == 0u && LAST_ROW >= _line.first.yLow)
    {
      if (result.ends == 0u)
      {
        result.line = RepackClipped(_line);
        return result;
      }
      result.ends = static_cast<std::uint8_t>(result.ends >> 1);
    }

    // 6502: LL83 -- with neither end on screen, four cheap rejections before any arithmetic.
    if ((result.ends & 0x80u) != 0u && BothEndsBeyondTheSameEdge(_line, _geometry))
    {
      result.rejected = true;
      return result;
    }

    const Slope slope = MeasureSlope(_line, _geometry, _math);

    // 6502: LL116 -- the gradient and its direction, where LL118 and LL120/LL123 read them. They
    // are the `Slope` value since M2-c-2; the two bytes are still written because `XX12` is what
    // the original works in and M2-c-3 is what takes it.
    _geometry.dotProducts[2] = slope.gradient;
    _geometry.dotProducts[3] = slope.direction;

    const bool nearEndOnScreen = result.ends != 0u && (result.ends & 0x80u) == 0u;
    if (!nearEndOnScreen)
    {
      // 6502: LL138 -- clip the near end.
      MovePointOnScreen(_line.first, slope, _math);

      if ((result.ends & 0x80u) == 0u)
      {
        // The far end was already on screen, so one clip was the whole job.
        result.line = RepackClipped(_line);
        return result;
      }

      // 6502: LL117 -- and if clipping did not actually bring it on screen, the line misses.
      if ((_line.first.xHigh | _line.first.yHigh) != 0u || _line.first.yLow >= SPACE_VIEW_BOTTOM)
      {
        result.rejected = true;
        return result;
      }
    }

    // 6502: LLX117 -- put the other end in the near slot and clip that too.
    SwapEnds(_line);
    MovePointOnScreen(_line.first, slope, _math);
    result.swap = static_cast<std::uint8_t>(result.swap - 1u); // 6502: SWAP steps down

    result.line = RepackClipped(_line);
    return result;
  }

  ClipResult ClipLine(Line16 _line, GeometryWorkspace& _geometry, MathWorkspace& _math, const ClipState& _clip) noexcept
  {
    // 6502: LL145 -- `SWAP` zeroed, which `LL147` does not do.
    return ClipLineKeepingSwap(_line, _geometry, _math, _clip, 0u, _line.second.xHigh);
  }

  namespace
  {

    /// 6502: LL15 and LL21 -- copy the ship's three orientation vectors into XX16 and scale each
    /// magnitude down by 197. Doubling the magnitude puts its top bit into the carry and the sign
    /// byte rotates it in, so what gets divided is the pair read as nine bits.
    void ScaleOrientation(const Ship& _work, GeometryWorkspace& _geometry) noexcept
    {
      const std::array<std::uint8_t, SHIP_BLOCK_SIZE> bytes = _work.ToBytes();
      for (int byte = 5; byte >= 0; --byte)
      {
        const std::size_t at = static_cast<std::size_t>(byte);
        _geometry.scaledOrientation[at] = bytes[SHIP_SIDE_OFFSET + at];
        _geometry.scaledOrientation[at + 6u] = bytes[SHIP_ROOF_OFFSET + at];
        _geometry.scaledOrientation[at + 12u] = bytes[SHIP_NOSE_OFFSET + at];
      }

      constexpr std::uint8_t SCALE = 197; // 6502: 197 into Q
      for (int index = 16; index >= 0; index -= 2)
      {
        const std::size_t at = static_cast<std::size_t>(index);
        const ShiftResult raised = RotateLeftValue(_geometry.scaledOrientation[at], false);
        _geometry.scaledOrientation[at] = DivideByLog(RotateLeft(_geometry.scaledOrientation[at + 1u], raised.carry).value, SCALE).value;
      }
    }

    /// 6502: the twenty-four instructions at the top of LL42 -- transpose XX16, so the three vectors
    /// become their own x, y and z components. `LL51` is then the same code doing a different
    /// rotation, which is why there is a transpose rather than a second routine.
    void TransposeOrientation(GeometryWorkspace& _geometry) noexcept
    {
      std::swap(_geometry.scaledOrientation[2], _geometry.scaledOrientation[6]);
      std::swap(_geometry.scaledOrientation[3], _geometry.scaledOrientation[7]);
      std::swap(_geometry.scaledOrientation[4], _geometry.scaledOrientation[12]);
      std::swap(_geometry.scaledOrientation[5], _geometry.scaledOrientation[13]);
      std::swap(_geometry.scaledOrientation[10], _geometry.scaledOrientation[14]);
      std::swap(_geometry.scaledOrientation[11], _geometry.scaledOrientation[15]);
    }

    /// 6502: LL89 -- the dot product of a face's normal in XX12 with the position in XX15, whose SIGN
    /// decides whether the face is drawn. What gets stored is the MAGNITUDE, so a face seen exactly
    /// edge-on comes out invisible.
    std::uint8_t FaceVisibility(Vector16 _vector, const GeometryWorkspace& _geometry) noexcept
    {
      // 6502: the first product, and the sign it is summed under.
      SignMag16 total{MultiplyByLog(_vector.x.lo, _geometry.dotProducts[0], false).value,
                      static_cast<std::uint8_t>(_geometry.dotProducts[1] ^ _vector.x.hi)};

      std::uint8_t term = MultiplyByLog(_vector.y.lo, _geometry.dotProducts[2], false).value;
      SignedSum combined = CombineSigned(static_cast<std::uint8_t>(_geometry.dotProducts[3] ^ _vector.y.hi), term, total);
      total = SignMag16{combined.value, combined.sign};

      term = MultiplyByLog(_vector.z.lo, _geometry.dotProducts[4], false).value;
      combined = CombineSigned(static_cast<std::uint8_t>(_vector.z.hi ^ _geometry.dotProducts[5]), term, total);

      // The sign is tested and the branch skips the zero, so a negative S keeps the answer.
      return ((combined.sign & 0x80u) != 0u) ? combined.value : std::uint8_t{0};
    }

    /*
     * 6502: the sixteen-bit add-or-subtract at LL49/LL52 and LL53/LL54 -- the ship's own coordinate
     * plus or minus the rotated vertex, under the two signs, negated again when the subtraction
     * crossed zero.
     *
     * The two halves are written differently in the original -- one subtracts from one, the other
     * complements and adds one, with the sign flip on opposite sides of the branch that increments
     * the high byte -- and they compute the same thing, which is why one function serves both.
     */
    [[nodiscard]] SignMag24 PlaceVertexAxis(std::uint8_t _productLow, std::uint8_t _productSign, const Ship& _work,
                                            std::size_t _axis) noexcept
    {
      const auto axis = _work.PositionAt(static_cast<std::uint8_t>(_axis)); // 6502: INWK,X to INWK+2,X

      // 6502: XX15(2 1 0) for x and XX15(5 4 3) for y -- the same six bytes, read as two 24-bit
      // sign-magnitude coordinates once the dot products are done with them (M2-c-2: a value).
      SignMag24 placed;
      placed.sgn = axis.sgn;

      if (((placed.sgn ^ _productSign) & 0x80u) == 0u)
      {
        const AddResult sum = AddWithCarry(_productLow, axis.lo, false);
        placed.lo = sum.value;
        placed.hi = AddWithCarry(axis.hi, 0, sum.carry).value;
        return placed;
      }

      const SubResult low = SubtractWithCarry(axis.lo, _productLow, true);
      placed.lo = low.value;
      const SubResult high = SubtractWithCarry(axis.hi, 0, low.carry);
      placed.hi = high.value;

      if (high.carry)
      {
        return placed;
      }

      placed.hi = static_cast<std::uint8_t>(high.value ^ 0xFFu);

      const SubResult negated = SubtractWithCarry(1, placed.lo, high.carry);
      placed.lo = negated.value;

      // The increment happens when the negation did NOT borrow -- which is only when the low byte
      // was zero and the carry rippled all the way up.
      if (negated.carry)
      {
        placed.hi = static_cast<std::uint8_t>(placed.hi + 1u);
      }

      placed.sgn = static_cast<std::uint8_t>(placed.sgn ^ 0x80u);
      return placed;
    }

    /// 6502: LL80 -- put a clipped line's four bytes on the ship's line heap.
    void PushHeapLine(LineHeap& _heap, HeapOffset _run, Line _line, std::uint8_t& _next) noexcept
    {
      _heap.Write(_run.Byte(_next), _line.x1);
      _heap.Write(_run.Byte(static_cast<std::uint16_t>(_next + 1u)), _line.y1);
      _heap.Write(_run.Byte(static_cast<std::uint16_t>(_next + 2u)), _line.x2);
      _heap.Write(_run.Byte(static_cast<std::uint16_t>(_next + 3u)), _line.y2);
      _next = static_cast<std::uint8_t>(_next + 4u);
    }

    /// Whether the two faces a nibble pair names are both invisible, which is what makes a vertex or
    /// an edge not worth drawing. 6502: the four visibility tests in parts 6 and 10.
    bool EitherFaceVisible(const GeometryWorkspace& _geometry, std::uint8_t _pair) noexcept
    {
      return _geometry.faceVisible[static_cast<std::size_t>(_pair & 0x0Fu)] != 0u || _geometry.faceVisible[static_cast<std::size_t>(_pair >> 4)] != 0u;
    }

  } // namespace

  /*
   * 6502: what `LL9`'s eleven entry points share (M4-b).
   *
   * `DrawShip` was 551 lines over seven annotated part blocks, taking thirteen arguments and
   * carrying four locals across them under comment rules. The rules are function boundaries now and
   * this is what the stages hand each other.
   *
   * IT IS FILE-LOCAL AND THAT IS THE POINT, not a way round P5. The ratchet's `aggregate-refs`
   * counts reference members of the argument-list structs in the HEADERS, because those are
   * signatures threaded through the whole library -- the thing M3-a spent a phase removing. This is
   * the opposite: it exists so that seven stages in ONE translation unit stop passing thirteen
   * arguments each, and no caller outside this file can see it. `LaserHit` and M4-a-2's stage
   * results are file-local for the same reason.
   *
   * THE FOUR ZERO-PAGE BYTES ARE NOT IN IT. `XX2`, `XX3`, `XX12` and `XX16` stay in
   * `GeometryWorkspace` because two of them have a reader OUTSIDE this routine -- `DOEXP` copies
   * the projected vertices off `XX3` to build the cloud (§4.3) -- so they are the universe's state
   * and not a frame's. What is here is only what the parts carry between themselves.
   */
  struct ShipRender
  {
    Canvas& canvas;
    GeometryWorkspace& geometry;
    MathWorkspace& math;
    const ClipState& clip;
    Projection& screen;
    Ship& work;
    LineHeap& heap;
    const Blueprint& blueprint;

    /// The wide surface (Resolution.md RS-2). A reference rather than a pointer: inside `DrawShip`
    /// there is always a universe, and the nullable pointers are the public routines'.
    Picture& picture;

    std::uint8_t detail = 31;               ///< 6502: XX4 -- how much detail the distance allows
    std::array<std::uint8_t, 9> position{}; ///< 6502: XX18 -- the position, halved, then rotated
    HeapOffset run{};                       ///< 6502: the ship's own block of the line heap
    std::uint8_t used = 1;                  ///< 6502: U -- heap bytes used, one because byte 0 is the count

  };

  /*
   * ---- part 1: is there anything to draw at all? ------------------------------------------------
   *
   * Four ways of not being drawn, and the caller performs the two that leave the routine: `LL25`
   * and `LL14` end in tail jumps into `PlanetDraw.cpp` and `Explosion.cpp`, so the stage answers
   * which one rather than making the call. `EE51` and the `EE55` cloud
   * seeding ARE part 1 and stay.
   */
  enum class Presence : std::uint8_t
  {
    Draw,     ///< fall through to `EE28` -- there is a ship to draw
    Body,     ///< 6502: LL25 -- the planet or the sun, which is a different routine
    Erased,   ///< 6502: EE51, which `LL14` also jumps to -- rubbed out and gone
    Exploded, ///< 6502: `LL14`'s other exit, into `DOEXP` -- off the screen and still burning
  };

  [[nodiscard]] Presence TestPresence(ShipRender& _render, Ship& _slot, ShipType _type, Rng& _rng, bool _carryIn) noexcept
  {
    // 6502: LL25 -- a negative type is the planet or the sun, which is a different routine.
    if (IsBody(_type))
    {
      return Presence::Body;
    }

    // 6502: bit 7 of NEWB -- scooped or docked, so take it off the screen and forget it.
    if (Has(_render.work.traits, TraitBit::Remove))
    {
      // 6502: EE51 as a tail call, and the flag it leaves is `LL9`'s exit, which nothing reads.
      static_cast<void>(EraseShip(_render.canvas, _render.work, _render.heap, _carryIn, &_render.picture));
      return Presence::Erased;
    }

    const std::uint8_t entryState = _render.work.state;
    if (!Has(entryState, ShipStateBit::Exploding) && Has(entryState, ShipStateBit::Killed))
    {
      // Killed and not yet exploding. Bits 6 and 7 are cleared by the same instruction that sets
      // bit 5, so the ship stops firing in the moment it starts to blow up.
      _render.work.state = Without(With(entryState, ShipStateBit::Exploding), ShipStateBit::Firing, ShipStateBit::Killed);

      // Written through INF into the ship's block in K% rather than into INWK, so that the
      // caller's copy back does not undo them.
      _slot.acceleration = 0;
      _slot.pitchCounter = 0;

      // 6502: EE51, then the six instructions and the EE55 loop that seed the cloud -- on the
      // carry the erase returns, which is the caller's when there was nothing to erase (§6.157).
      const bool carry = EraseShip(_render.canvas, _render.work, _render.heap, _carryIn, &_render.picture);
      SeedExplosionCloud(_render.heap, _render.work.heap, _render.blueprint.explosionCount, _rng, carry); // 6502: (XX0),7
    }

    // 6502: EE28 / EE49 and LL10 -- four ways of being not worth drawing, sharing one exit. The
    // two coordinate tests are sixteen-bit: |x| or |y| at least as large as z puts the ship outside
    // a ninety-degree view whatever the projection would make of it.
    bool gone = (_render.work.z.sgn & 0x80u) != 0u;
    if (!gone)
    {
      gone = _render.work.z.hi >= 192u;
    }
    for (std::size_t axis = 0; !gone && axis < 2u; ++axis)
    {
      const auto other = _render.work.PositionAt((axis == 0u) ? SHIP_X_OFFSET : SHIP_Y_OFFSET);
      const SubResult low = SubtractWithCarry(other.lo, _render.work.z.lo, true);
      gone = SubtractWithCarry(other.hi, _render.work.z.hi, low.carry).carry;
    }

    if (gone)
    {
      // 6502: LL14.
      if (!Has(_render.work.state, ShipStateBit::Exploding))
      {
        // 6502: EE51 -- and the flag is `LL9`'s exit
        static_cast<void>(EraseShip(_render.canvas, _render.work, _render.heap, false, &_render.picture));
        return Presence::Erased;
      }

      _render.work.state = Without(_render.work.state, ShipStateBit::OnScreen);
      return Presence::Exploded;
    }

    return Presence::Draw;
  }

  /*
   * ---- part 2: how far away is it, and is that too far? ----------------------------------------
   *
   * Writes `XX4` into the frame, and answers whether a dot will do. The caller draws the dot,
   * because `LL13` ends in a tail jump to `SHPPT` and this stage's job is the measurement.
   */
  enum class Range : std::uint8_t
  {
    Detailed, ///< close enough for the vertices and the edges
    Dot,      ///< 6502: LL13 -- past the blueprint's own visibility distance
  };

  [[nodiscard]] Range MeasureRange(ShipRender& _render) noexcept
  {
    // Blueprint byte 6 is a vertex's offset in XX3, and 255 there means "this one did not
    // project". The laser line in part 9 reads it back and gives up when it is still 255.
    const std::uint8_t laserVertex = _render.blueprint.laserVertex;
    _render.geometry.projectedVertices[laserVertex] = 255;
    _render.geometry.projectedVertices[static_cast<std::size_t>(laserVertex) + 1u] = 255;

    // z divided by sixteen into (A T), and then by another eight. The rotate after the fourth
    // shift picks up the carry that shift left, so the two halves are one number and not two.
    std::uint8_t distanceLow = _render.work.z.lo;
    std::uint8_t distanceHigh = _render.work.z.hi;
    for (int shift = 0; shift < 3; ++shift)
    {
      const bool into = (distanceHigh & 0x01u) != 0u;
      distanceHigh = static_cast<std::uint8_t>(distanceHigh >> 1);
      distanceLow = RotateRight(distanceLow, into).value;
    }
    const bool spare = (distanceHigh & 0x01u) != 0u;
    distanceHigh = static_cast<std::uint8_t>(distanceHigh >> 1);

    if (distanceHigh == 0u)
    {
      _render.detail = static_cast<std::uint8_t>(RotateRight(distanceLow, spare).value >> 3);
    }
    else if (_render.blueprint.visibility < _render.work.z.hi && !Has(_render.work.state, ShipStateBit::Exploding))
    {
      // 6502: LL13 -- past the blueprint's own visibility distance, so a dot will do.
      return Range::Dot;
    }

    return Range::Detailed;
  }

  /// ---- part 3: the orientation vectors, scaled -------------------------------------------------
  void ScaleShip(ShipRender& _render) noexcept
  {
    ScaleOrientation(_render.work, _render.geometry);

    const std::array<std::uint8_t, SHIP_BLOCK_SIZE> position = _render.work.ToBytes();
    for (int byte = 8; byte >= 0; --byte)
    {
      _render.position[static_cast<std::size_t>(byte)] = position[static_cast<std::size_t>(byte)];
    }
    _render.geometry.faceVisible[15] = 255;
  }

  /// ---- parts 4 and 5: which faces can be seen --------------------------------------------------
  void SelectFaces(ShipRender& _render) noexcept
  {
    const std::uint8_t faceBytes = _render.blueprint.faceBytes;

    if (Has(_render.work.state, ShipStateBit::Exploding))
    {
      // 6502: EE30 -- an exploding ship shows every face and every vertex, so that the whole cloud
      // can be built out of them.
      for (int face = faceBytes >> 2; face >= 0; --face)
      {
        _render.geometry.faceVisible[static_cast<std::size_t>(face)] = 255;
      }
      _render.detail = 0;
    }
    else if (faceBytes != 0u)
    {
      // 6502: EE29 -- halve the ship's position until its z fits in a byte, counting the halvings
      // on top of the blueprint's own scale in byte 18.
      std::uint8_t shifts = _render.blueprint.normalShifts;
      std::uint8_t z = _render.position[7];
      while (z != 0u)
      {
        ++shifts;

        const bool intoY = (_render.position[4] & 0x01u) != 0u;
        _render.position[4] = static_cast<std::uint8_t>(_render.position[4] >> 1);
        _render.position[3] = RotateRight(_render.position[3], intoY).value;

        const bool intoX = (_render.position[1] & 0x01u) != 0u;
        _render.position[1] = static_cast<std::uint8_t>(_render.position[1] >> 1);
        _render.position[0] = RotateRight(_render.position[0], intoX).value;

        const bool intoZ = (z & 0x01u) != 0u;
        z = static_cast<std::uint8_t>(z >> 1);
        _render.position[6] = RotateRight(_render.position[6], intoZ).value;
      }

      // 6502: LL91 -- the position, rotated into the ship's own frame. `XX15`'s six bytes are the
      // three sign-magnitude pairs `LL51` reads, which is a `Vector16` since M2-c-2.
      DotProducts(Vector16{SignMag16{_render.position[0], _render.position[2]}, SignMag16{_render.position[3], _render.position[5]},
                           SignMag16{_render.position[6], _render.position[8]}},
                  _render.geometry);
      _render.position[0] = _render.geometry.dotProducts[0];
      _render.position[2] = _render.geometry.dotProducts[1];
      _render.position[3] = _render.geometry.dotProducts[2];
      _render.position[5] = _render.geometry.dotProducts[3];
      _render.position[6] = _render.geometry.dotProducts[4];
      _render.position[8] = _render.geometry.dotProducts[5];

      // 6502: `V` is built from bytes 4 and 17 of the blueprint and points at the faces; in this
      // port it is `at`, because the blueprint carries them as a span, so the pointer set-up is an
      // index starting at zero and nothing else (M4-b).
      std::uint8_t at = 0;
      do
      {
        // 6502: LL86 -- a face whose own distance is under the ship's is taken as visible without
        // the arithmetic.
        const std::uint8_t flags = _render.blueprint.faces[at];
        _render.geometry.dotProducts[1] = flags;

        if ((flags & 0x1Fu) < _render.detail)
        {
          _render.geometry.faceVisible[static_cast<std::size_t>(at >> 2)] = 255;
          at = static_cast<std::uint8_t>(at + 4u);
          continue;
        }

        // 6502: LL87 -- the face's normal, with its three sign bits spread out by doubling.
        _render.geometry.dotProducts[3] = static_cast<std::uint8_t>(flags << 1);
        _render.geometry.dotProducts[5] = static_cast<std::uint8_t>(flags << 2);
        _render.geometry.dotProducts[0] = _render.blueprint.faces[at + 1u];
        _render.geometry.dotProducts[2] = _render.blueprint.faces[at + 2u];
        _render.geometry.dotProducts[4] = _render.blueprint.faces[at + 3u];

        Vector16 normal; // 6502: XX15's six bytes, the face's normal plus the ship's position

        if (shifts >= 4u) // 6502: XX17, the shift count part 3 left
        {
          // 6502: LL143 -- the position is already small enough to use as it stands.
          normal = Vector16{SignMag16{_render.position[0], _render.position[2]}, SignMag16{_render.position[3], _render.position[5]},
                            SignMag16{_render.position[6], _render.position[8]}};
        }
        else
        {
          /*
           * 6502: LL92 to LL94, with `ovflw` as the retry.
           *
           * Scale the normal down by the shift count and add the position to it, one axis at a
           * time. Any of the three overflowing sends the whole thing back to the start with the
           * POSITION halved and the scale reset to one -- so this is a loop that can run several
           * times, and the partial results it leaves behind on the way are what the original
           * leaves too.
           */
          std::uint8_t scale = shifts;
          for (;;)
          {
            std::uint8_t first = _render.geometry.dotProducts[0];
            std::uint8_t second = _render.geometry.dotProducts[2];
            std::uint8_t third = _render.geometry.dotProducts[4];
            for (std::uint8_t left = scale; left != 0u; --left)
            {
              first = static_cast<std::uint8_t>(first >> 1);
              second = static_cast<std::uint8_t>(second >> 1);
              third = static_cast<std::uint8_t>(third >> 1);
            }

            // 6502: `LL38` on the z pair, and the same for x and y: each is the halved coordinate
            // under the ship's sign, plus the vertex.
            const SignedSum alongZ = CombineSigned(_render.position[8], _render.position[6], SignMag16{third, _render.geometry.dotProducts[5]});
            if (!alongZ.carry)
            {
              normal.z = SignMag16{alongZ.value, alongZ.sign};

              const SignedSum alongX = CombineSigned(_render.position[2], _render.position[0], SignMag16{first, _render.geometry.dotProducts[1]});
              if (!alongX.carry)
              {
                normal.x = SignMag16{alongX.value, alongX.sign};

                const SignedSum alongY =
                  CombineSigned(_render.position[5], _render.position[3], SignMag16{second, _render.geometry.dotProducts[3]});
                if (!alongY.carry)
                {
                  normal.y = SignMag16{alongY.value, alongY.sign};
                  break;
                }
              }
            }

            // 6502: ovflw.
            _render.position[0] = static_cast<std::uint8_t>(_render.position[0] >> 1);
            _render.position[6] = static_cast<std::uint8_t>(_render.position[6] >> 1);
            _render.position[3] = static_cast<std::uint8_t>(_render.position[3] >> 1);
            scale = 1;
          }
        }

        _render.geometry.faceVisible[static_cast<std::size_t>(at >> 2)] = FaceVisibility(normal, _render.geometry);
        at = static_cast<std::uint8_t>(at + 4u);
      } while (at < faceBytes); // 6502: XX20
    }
  }

  /// ---- parts 6 to 8: project the vertices the visible faces touch ------------------------------
  void ProjectVertices(ShipRender& _render) noexcept
  {
    TransposeOrientation(_render.geometry);

    // 6502: XX0+20 -- V is the vertices here, and the loop's own `vertex` is it: the blueprint
    // carries them as a span, so the pointer set-up is an index starting at zero (M4-b).

    /*
     * 6502: CNT -- where in `XX3` the next projected vertex goes, four bytes at a time.
     *
     * Its carry is load-bearing: `LL50` adds four to it and then adds six to `XX17` ON THAT CARRY,
     * so a heap that has filled past 255 ends the vertex loop. Both are locals since M2-c-3 and
     * the addition is written out below, carry and all.
     */
    std::uint8_t vertexSlot = 0;

    for (std::uint8_t vertex = 0;;)
    {
      const std::uint8_t flags = _render.blueprint.vertices[vertex + 3u];

      const bool nearEnough = (flags & 0x1Fu) >= _render.detail;
      const bool visible = nearEnough && (EitherFaceVisible(_render.geometry, _render.blueprint.vertices[vertex + 4u]) ||
                                          EitherFaceVisible(_render.geometry, _render.blueprint.vertices[vertex + 5u]));

      if (visible)
      {
        // 6502: LL49 -- the vertex's three sign bits, spread out by doubling, then rotated into
        // the player's frame and added to the ship's own position.
        DotProducts(Vector16{SignMag16{_render.blueprint.vertices[vertex], flags},
                             SignMag16{_render.blueprint.vertices[vertex + 1u], static_cast<std::uint8_t>(flags << 1)},
                             SignMag16{_render.blueprint.vertices[vertex + 2u], static_cast<std::uint8_t>(flags << 2)}},
                    _render.geometry);

        // 6502: XX15's six bytes again, now as x in (2 1 0) and y in (5 4 3).
        SignMag24 across = PlaceVertexAxis(_render.geometry.dotProducts[0], _render.geometry.dotProducts[1], _render.work, SHIP_X_OFFSET);
        SignMag24 down = PlaceVertexAxis(_render.geometry.dotProducts[2], _render.geometry.dotProducts[3], _render.work, SHIP_Y_OFFSET);

        // 6502: LL55 / LL56 / LL140 -- and z, which is a plain sixteen-bit add or subtract with a
        // floor of four rather than a sign-magnitude one, because a vertex behind the player has
        // to be pulled in front of it before anything is divided by it. `(U T)` is this loop's own
        // since M2-c-3.
        SignMag16 depth{}; // 6502: (U T) -- the vertex's distance
        if ((_render.geometry.dotProducts[5] & 0x80u) == 0u)
        {
          const AddResult sum = AddWithCarry(_render.geometry.dotProducts[4], _render.work.z.lo, false);
          depth.lo = sum.value;
          depth.hi = AddWithCarry(_render.work.z.hi, 0, sum.carry).value;
        }
        else
        {
          const SubResult low = SubtractWithCarry(_render.work.z.lo, _render.geometry.dotProducts[4], true);
          depth.lo = low.value;
          const SubResult high = SubtractWithCarry(_render.work.z.hi, 0, low.carry);
          depth.hi = high.value;

          if (!high.carry || (high.value == 0u && low.value < 4u))
          {
            depth.hi = 0;
            depth.lo = 4;
          }
        }

        // 6502: LL57 -- halve all three until the two coordinates and the distance fit in a byte
        // each, so that the division below is an eight-bit one.
        while ((depth.hi | across.hi | down.hi) != 0u)
        {
          const bool intoX = (across.hi & 0x01u) != 0u;
          across.hi = static_cast<std::uint8_t>(across.hi >> 1);
          across.lo = RotateRight(across.lo, intoX).value;

          const bool intoY = (down.hi & 0x01u) != 0u;
          down.hi = static_cast<std::uint8_t>(down.hi >> 1);
          down.lo = RotateRight(down.lo, intoY).value;

          const bool intoZ = (depth.hi & 0x01u) != 0u;
          depth.hi = static_cast<std::uint8_t>(depth.hi >> 1);
          depth.lo = RotateRight(depth.lo, intoZ).value;
        }

        // 6502: LL60 to LL70 -- the projection itself, and the only place in the port where the
        // divide is picked by which of two routines can do it: LL28 when the coordinate is smaller
        // than the distance, LL61 when it is not.
        std::uint8_t x = vertexSlot;

        // 6502: the distance into `Q`, and the divide is (U R) = 256 * x / distance. `LL28`
        // leaves U as it was, which is the zero the halving loop above ended on. `Q` is written as
        // well as read here because, for the last vertex of the last ship drawn, it is the frame's Q
        // the altitude check reads (`EndFlightFrame`) -- unless the clipper writes it after.
        const std::uint8_t distance = depth.lo;
        _render.math.lastDivisor = distance;
        Quotient16 projectedX{depth.hi, 0};
        if (across.lo < distance)
        {
          projectedX.low = DivideByLog(across.lo, distance).value;
        }
        else
        {
          projectedX = DivideWideByLog(across.lo, distance, depth.hi);
        }

        if ((across.sgn & 0x80u) != 0u)
        {
          // 6502: LL62 -- 128 - (U R), for a vertex to the left of centre.
          const SubResult low = SubtractWithCarry(128, projectedX.low, true);
          _render.geometry.projectedVertices[x] = low.value;
          ++x;
          _render.geometry.projectedVertices[x] = SubtractWithCarry(0, projectedX.high, low.carry).value;
        }
        else
        {
          const AddResult low = AddWithCarry(projectedX.low, 128, false);
          _render.geometry.projectedVertices[x] = low.value;
          ++x;
          _render.geometry.projectedVertices[x] = AddWithCarry(projectedX.high, 0, low.carry).value;
        }

        // 6502: LL66 -- and the same again for y, with U cleared first because `LL28` does not
        // write it and the last vertex's value would otherwise be added in.
        Quotient16 projectedY{0, 0};
        if (down.lo < distance)
        {
          projectedY.low = DivideByLog(down.lo, distance).value;
        }
        else
        {
          projectedY = DivideWideByLog(down.lo, distance, 0);
        }

        ++x;
        if ((down.sgn & 0x80u) != 0u)
        {
          // 6502: LL70 -- below the centre of the view.
          const AddResult low = AddWithCarry(SPACE_VIEW_CENTRE_Y, projectedY.low, false);
          _render.geometry.projectedVertices[x] = low.value;
          ++x;
          _render.geometry.projectedVertices[x] = AddWithCarry(0, projectedY.high, low.carry).value;
        }
        else
        {
          const SubResult low = SubtractWithCarry(SPACE_VIEW_CENTRE_Y, projectedY.low, true);
          _render.geometry.projectedVertices[x] = low.value;
          ++x;
          _render.geometry.projectedVertices[x] = SubtractWithCarry(0, projectedY.high, low.carry).value;
        }

      }

      // 6502: LL50 -- on to the next vertex, six bytes along, and stop when the count runs out or
      // the index wraps. The carry out of CNT's addition is what feeds XX17's, so a heap that has
      // filled past 255 ends the loop as well.
      const AddResult nextCnt = AddWithCarry(vertexSlot, 4, false);
      vertexSlot = nextCnt.value;
      const AddResult nextVertex = AddWithCarry(vertex, 6, nextCnt.carry);
      vertex = nextVertex.value;

      if (nextVertex.carry || vertex >= _render.blueprint.vertexBytes) // 6502: XX20
      {
        break;
      }
    }
  }

  /*
   * ---- part 9: the ship is on the screen from here, and the laser goes on the heap --------------
   *
   * Answers false for the exploding path, which is the caller's `DrawExplosion`. Everything else --
   * `EE31`'s erase, the `OnScreen` bit and the laser beam -- is this stage's, and it opens the heap
   * run the edges are pushed onto.
   */
  [[nodiscard]] bool OpenHeapRun(ShipRender& _render) noexcept
  {
    if (Has(_render.work.state, ShipStateBit::Exploding))
    {
      _render.work.state = With(_render.work.state, ShipStateBit::OnScreen);
      return false;
    }

    // 6502: EE31 -- rub out the last frame's ship, then mark this one as being on the screen. The
    // run is the frame's from here: parts 10 and 11 push onto the same block and count against the
    // same total, which is why `U` and the offset travel together (M4-b).
    _render.run = _render.work.heap;
    if (Has(_render.work.state, ShipStateBit::OnScreen))
    {
      DrawShipLines(_render.canvas, _render.heap, _render.run, &_render.picture);
    }
    _render.work.state = With(_render.work.state, ShipStateBit::OnScreen);

    // 6502: U -- how many bytes of the heap this ship has used, which starts at one because byte 0
    // is the count itself.
    _render.used = 1;

    if (Has(_render.work.state, ShipStateBit::Firing))
    {
      _render.work.state = Without(_render.work.state, ShipStateBit::Firing);

      const std::size_t muzzle = _render.blueprint.laserVertex;
      Line16 beam;
      beam.first.xLow = _render.geometry.projectedVertices[muzzle];
      beam.first.xHigh = _render.geometry.projectedVertices[muzzle + 1u];

      // Both bytes are tested by incrementing them, so 255 -- which is what part 2 wrote there and
      // what a vertex that did not project leaves -- is the one value that means "no laser".
      if (static_cast<std::uint8_t>(beam.first.xLow + 1u) != 0u && static_cast<std::uint8_t>(beam.first.xHigh + 1u) != 0u)
      {
        beam.first.yLow = _render.geometry.projectedVertices[muzzle + 2u];
        beam.first.yHigh = _render.geometry.projectedVertices[muzzle + 3u];
        beam.second.xLow = 0;
        beam.second.xHigh = 0;

        // 6502: the far end's y is `XX12(1 0)`, which is where `LL145` reads it and where the port
        // keeps writing it: `XX12` is `LL9`'s frame until M2-c-3.
        _render.geometry.dotProducts[1] = 0;
        _render.geometry.dotProducts[0] = _render.work.z.lo;
        beam.second.yLow = _render.geometry.dotProducts[0];
        beam.second.yHigh = _render.geometry.dotProducts[1];

        // The laser fires towards the player, so the far end is the origin -- and to the left of
        // it when the ship is to the left, which is the whole of this `DEC`.
        if ((_render.work.x.sgn & 0x80u) != 0u)
        {
          beam.second.xLow = 255;
        }

        const ClipResult clipped = ClipLine(beam, _render.geometry, _render.math, _render.clip);
        if (!clipped.rejected)
        {
          PushHeapLine(_render.heap, _render.run, clipped.line, _render.used);
        }
      }
    }

    return true;
  }

  /// ---- parts 10 and 11: the edges, and the heap's own length byte ------------------------------
  void PushEdges(ShipRender& _render) noexcept
  {
    std::uint16_t walker = 0;   // 6502: V(1 0) -- the edges, which the blueprint carries as a span
    std::uint8_t heapLimit = 0; // 6502: T1 -- how many heap bytes this blueprint allows
    std::uint8_t edgeIndex = 0; // 6502: XX17

    // 6502: `V` is built from bytes 3 and 16 of the blueprint and points at the edges, which the
    // blueprint carries as a span here; the index starts at 0.
    walker = 0;
    heapLimit = _render.blueprint.heapBytes;

    // 6502: SWAP, which `LL147` decrements and nothing in `LL9` reads: `LOIN` zeroes it before it
    // sets it again, and `WPLS2`'s reader is the ball's. The accumulation is the byte's, so the
    // port carries it (M2-c-2).
    std::uint8_t swap = 0;

    for (;;)
    {
      // 6502: LL75 -- four bytes per edge: how far away it stays visible, the two faces it joins,
      // and the two vertices it runs between.
      const std::uint8_t distance = _render.blueprint.edges[walker];
      if (distance >= _render.detail && EitherFaceVisible(_render.geometry, _render.blueprint.edges[walker + 1u]))
      {
        const std::size_t from = _render.blueprint.edges[walker + 2u];
        const std::size_t to = _render.blueprint.edges[walker + 3u];

        Line16 edge;
        edge.first.xHigh = _render.geometry.projectedVertices[from + 1u];
        edge.first.xLow = _render.geometry.projectedVertices[from];
        edge.first.yLow = _render.geometry.projectedVertices[from + 2u];
        edge.first.yHigh = _render.geometry.projectedVertices[from + 3u];
        edge.second.xLow = _render.geometry.projectedVertices[to];

        // 6502: `XX12(1 0)` again -- the far end's y, in `LL9`'s frame until M2-c-3.
        _render.geometry.dotProducts[1] = _render.geometry.projectedVertices[to + 3u];
        _render.geometry.dotProducts[0] = _render.geometry.projectedVertices[to + 2u];
        edge.second.yHigh = _render.geometry.dotProducts[1];
        edge.second.yLow = _render.geometry.dotProducts[0];
        edge.second.xHigh = _render.geometry.projectedVertices[to + 1u];

        // 6502: `LL147` is entered with `XX15+5` in the accumulator, and `SWAP` accumulates across
        // the edges rather than being zeroed for each.
        const ClipResult clipped = ClipLineKeepingSwap(edge, _render.geometry, _render.math, _render.clip, swap, edge.second.xHigh);
        swap = clipped.swap;

        if (!clipped.rejected)
        {
          /*
           * NOTHING TWINS HERE SINCE RS-3, and the absence is the point (Resolution.md §4.1 and the
           * journal). RS-2 pushed a separately doubled and re-clipped line onto a parallel wide heap
           * at this exact spot. It does not need to exist: `PushHeapLine` below writes the four
           * bytes, and `DrawShipLines2x` reads those same four and doubles them itself, so the wide
           * line cannot disagree with the faithful one about anything -- and a whole heap, a
           * `Universe` field and a state-hash exclusion went with it.
           */

          // 6502: LL80 -- and stop as soon as the heap this blueprint asked for is full.
          PushHeapLine(_render.heap, _render.run, clipped.line, _render.used);
          if (_render.used >= heapLimit)
          {
            break;
          }
        }
      }

      // 6502: LL78.
      edgeIndex = static_cast<std::uint8_t>(edgeIndex + 1u);
      if (edgeIndex >= _render.blueprint.edgeCount) // 6502: XX20
      {
        break;
      }
      walker = static_cast<std::uint16_t>(walker + 4u);
    }

    // 6502: LL81 -- the heap's length goes in byte 0, and then it is drawn.
    StoreLineCountAndDraw(_render.canvas, _render.heap, _render.run, _render.used, &_render.picture);
  }

  /*
   * 6502: LL9 -- the ship renderer, as the seven stages its part blocks always were (M4-b).
   *
   * Every stage answers and this performs: the two tail jumps, the dot and the explosion are here,
   * over the universe, and no stage sees more of it than its `ShipRender` frame.
   */
  namespace
  {
    /// 6502: `LL14`'s tail jump into `DOEXP` -- age the cloud by one frame and draw it, which is how the last
    /// frame is erased as well as how this one appears. `INWK` is the exploding ship and `XX3` the
    /// vertices part 8 projected, which `DOEXP` copies onto the ship's line heap on its first frame.
    void DrawExplosion(Universe& _universe) noexcept
    {
      DrawExplosionCloud(_universe.canvas, _universe.math, _universe.rng, _universe.work, _universe.heap, _universe.geometry,
                         _universe.bubble, _universe.video, _universe.memoryMap, &_universe.picture);
    }
  } // namespace

  void DrawShip(Universe& _universe, Ship& _slot, bool _carryIn) noexcept
  {
    ShipRender render{_universe.canvas, _universe.geometry, _universe.math,   _universe.clip,    _universe.projection,
                      _universe.work,   _universe.heap,     *_universe.flight.blueprint, _universe.picture};

    switch (TestPresence(render, _slot, _universe.flight.type, _universe.rng, _carryIn)) // 6502: part 1
    {
    case Presence::Draw:
      break;
    case Presence::Body:
      // 6502: LL25 -- a tail jump to `PLANET`, taken for a type with bit 7 set. `INWK` is the body
      // and `TYPE`
      // decides which of the two it is, exactly as the tail jump does.
      DrawPlanetOrSun(_universe.canvas, _universe.heaps, _universe.geometry, _universe.math, _universe.clip, _universe.rng, _universe.work,
                      _universe.projection, _universe.flight.type, &_universe.picture);
      return;
    case Presence::Erased:
      return; // 6502: EE51, and the flag it leaves is `LL9`'s exit, which nothing reads
    case Presence::Exploded:
      DrawExplosion(_universe); // 6502: `LL14`'s tail jump into `DOEXP`
      return;
    }

    if (MeasureRange(render) == Range::Dot) // 6502: part 2
    {
      // 6502: `LL13`'s tail jump to `SHPPT`
      DrawShipAsPoint(_universe.canvas, _universe.work, _universe.heap, _universe.math, _universe.projection, &_universe.picture);
      return;
    }

    ScaleShip(render);       // 6502: part 3
    SelectFaces(render);     // 6502: parts 4 and 5
    ProjectVertices(render); // 6502: parts 6 to 8

    if (!OpenHeapRun(render)) // 6502: part 9
    {
      DrawExplosion(_universe);
      return;
    }

    PushEdges(render); // 6502: parts 10 and 11, and `LL81`'s count byte with them
  }

} // namespace Elite
