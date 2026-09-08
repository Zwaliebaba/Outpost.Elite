#include "pch.h"

#include "Scanner.h"

#include "Arith.h"
#include "Dashboard2x.h"
#include "EliteTypes.h"
#include "LookupTables.h"

#include <span>

namespace Elite
{

  // ---- the scanner ------------------------------------------------------------------------------

  void DrawScannerBlip(Canvas& _canvas, const Ship& _ship, ShipType _type, std::uint8_t _view, Picture* _picture) noexcept
  {
    // 6502: SCR1 -- no dashboard on any view but the space view, so no scanner.
    if (_view != 0u)
    {
      return;
    }

    // 6502: bit 4 of the state byte is "show this on the scanner", and it is cleared for the ships
    // that have no blip at all.
    if (!Has(_ship.state, ShipStateBit::OnScanner))
    {
      return;
    }

    // 6502: a negative type goes to SCR1 -- the planet and the sun are 128 and 129, not shown.
    if (IsBody(_type))
    {
      return;
    }

    // The index is a ship TYPE, 1 to `SHIP_TYPE_COUNT`, which is what the table is sized by and
    // what `FRIN` can hold. The `BMI` above has already taken the planet and the sun out of it.
    const PixelPattern pattern = PatternOf(SCANNER_COLOUR_TABLE[Byte(_type)]); // 6502: into COL

    /*
     * 6502: SCR1 -- the three high bytes ORed and the top two bits tested.
     *
     * Off the scanner if ANY of the three high bytes reaches 64, which is the one range check the
     * routine makes -- everything below works in the top six bits of a coordinate and would wrap
     * rather than clip. It is a test of the three ORed together and not of each in turn, which is
     * the same answer for a check of "is any bit 6 or 7 set anywhere".
     */
    if (((_ship.x.hi | _ship.y.hi | _ship.z.hi) & 0xC0u) != 0u)
    {
      return;
    }

    /*
     * 6502: the x coordinate, and the negation is the ordinary sign-magnitude one -- except that
     * the `CLC` is before the branch, so the positive path adds 123 with a clear carry and the
     * negative path adds it with whatever the negation's own addition produced. A ship at x_hi = 0
     * with the sign bit
     * set therefore lands one pixel right of one at x_hi = 0 without it: 124 rather than 123.
     */
    bool carry = false; // 6502: the carry cleared
    std::uint8_t across = _ship.x.hi;
    if ((_ship.x.sgn & 0x80u) != 0u) // 6502: SC2, for a positive x
    {
      const AddResult negated = AddWithCarry(static_cast<std::uint8_t>(across ^ 0xFFu), 1u, carry);
      across = negated.value;
      carry = negated.carry;
    }

    const std::uint8_t x1 = AddWithCarry(across, 123u, carry).value; // 6502: SC2 -- 123 added, into X1

    /*
     * 6502: the scanner's ellipse is drawn around a horizontal line, and this is where on that
     * line the ship's DEPTH puts it -- z_hi / 4, and then the whole thing inverted, because screen
     * rows count downwards and z counts away from the player.
     *
     * `SC` is the byte it goes in and it is NOT the screen pointer here. It becomes one further
     * down, when `CPIX4` overwrites it; until then it is the row the stick is drawn back to.
     */
    std::uint8_t depth = static_cast<std::uint8_t>(_ship.z.hi >> 2);
    bool depthCarry = false;         // 6502: the carry cleared
    if ((_ship.z.sgn & 0x80u) != 0u) // 6502: SC3, for a positive z
    {
      depth = static_cast<std::uint8_t>(depth ^ 0xFFu);
      depthCarry = true; // 6502: SEC -- so the one's complement and the carry make a negation
    }

    const std::uint8_t ground = static_cast<std::uint8_t>(AddWithCarry(depth, 83u, depthCarry).value ^ 0xFFu);

    /*
     * 6502: and the height above that line, which is y_hi / 2 -- with the test the other way up.
     *
     * The branch to `SCD6` skips the negation for a NEGATIVE y, so it is a positive y that gets
     * complemented.
     * That is not an inconsistency with the depth above: screen rows increase downwards, so a ship
     * above the plane of flight has to move to a smaller row number.
     */
    std::uint8_t height = static_cast<std::uint8_t>(_ship.y.hi >> 1);
    bool heightCarry = false;        // 6502: the carry cleared
    if ((_ship.y.sgn & 0x80u) == 0u) // 6502: SCD6, for a negative y
    {
      height = static_cast<std::uint8_t>(height ^ 0xFFu);
      heightCarry = true; // 6502: SEC
    }

    std::uint8_t row = AddWithCarry(height, ground, heightCarry).value; // 6502: SCD6 -- the ground row added

    // 6502: clamped into the dashboard between 146 and 198, and the second comparison sees the
    // clamped value, which is why the two cannot both fire.
    if (row < 146u)
    {
      row = 146u;
    }
    else if (row >= 199u)
    {
      row = 198u;
    }

    const std::uint8_t y1 = row; // 6502: into Y1

    // 6502: the ground row taken off, and the flags pushed -- how tall the stick is, and which way
    // up. The carry is the sign and it survives `CPIX4` on the stack, because the move to X below
    // sets N and Z but not C.
    const SubResult stick = SubtractWithCarry(row, ground, true);

    if (_picture != nullptr)
    {
      /*
       * `x_lo` is a byte the game maintains on every ship in the bubble and `SCAN` never reads: the
       * fraction under `x_hi`, which is one blip position of four at twice the resolution. It is
       * signed the way the magnitude is -- `SC2` negates a negative x before adding 123, so a
       * larger fraction moves the blip LEFT there and right here (Resolution.md section 5.2).
       *
       * The vertical fractions under `z_lo` and `y_lo` are the same kind of byte and are NOT taken
       * this slice, which is a scoping call rather than an oversight: the row is a clamped sum of
       * two negated magnitudes and reconstructing it at twice the scale is a second copy of `SCAN`'s
       * arithmetic to keep in step, for one hi-res row of a fifty-two row scale (§13).
       */
      const int acrossBit = ((_ship.x.sgn & 0x80u) != 0u) ? -static_cast<int>(_ship.x.lo >> 7) : static_cast<int>(_ship.x.lo >> 7);
      const bool up = stick.carry;
      const std::uint8_t height = up ? stick.value : static_cast<std::uint8_t>(0u - stick.value);

      DrawScannerBlip2x(*_picture, _canvas, x1, acrossBit, y1, height, up, pattern);
    }

    const CellCursor cursor = PlotBlock(_canvas, x1, y1, pattern); // 6502: CPIX4

    /*
     * 6502: the mask two entries on from the dot's, masked by the colour.
     *
     * The stick has the same pixel pattern as the dot's RIGHT-hand pixel, so it comes out of the
     * right side of the dot rather than the middle -- and `X1` stops being a coordinate here and
     * becomes that pattern. The cursor's cell has already followed the same wrap, so the two agree
     * about which character block the stick belongs in.
     */
    const std::uint8_t stickMask =
      static_cast<std::uint8_t>(MULTICOLOUR_MASK_TABLE[cursor.pixel + 2u] & PatternByte(pattern)); // 6502: into X1

    // 6502: a zero height returns at once -- a ship exactly on the plane of flight has a dot and
    // no stick.
    if (stick.value == 0u)
    {
      return;
    }

    std::uint16_t address = cursor.address;
    std::uint8_t within = cursor.row;

    if (stick.carry)
    {
      /*
       * 6502: VLL1 -- upwards, one `DEY` a row, and a step back of 320 bytes whenever that walks
       * off the top of the character block. `CPIX4` finished on the dot's UPPER row, so the first
       * pixel of the stick is the row directly above it and there is nothing to skip.
       */
      std::uint8_t remaining = stick.value;
      do
      {
        if (within == 0u)
        {
          within = 7u;
          address = static_cast<std::uint16_t>(address - 0x140u); // 6502: &140 off the address, in two bytes
        }
        else
        {
          --within;
        }

        _canvas.ExclusiveOr(static_cast<std::uint16_t>(address + within), stickMask);
        --remaining; // 6502: VLL1, while the count is not zero
      } while (remaining != 0u);

      return;
    }

    /*
     * 6502: VL3 -- downwards, and it steps TWICE before the first pixel.
     *
     * `VL3` is not the loop: it is one extra `INY` that falls into it, and the reason is where the
     * cursor is rather than an off-by-one. `CPIX4` draws the dot's lower row first, decrements
     * `Y1`, then draws the upper one -- so it finishes on the TOP of a two-row dot. Going up, one
     * `DEY` clears it; going down, it takes one step to pass the row below and a second to get
     * clear. Both sticks therefore start one row from the dot's edge, which is the symmetry the
     * two different step counts are hiding.
     *
     * The carry into that addition is always set, because the only way to reach it is through a
     * comparison that found the row index equal to 8.
     */
    auto stepDown = [&address, &within]() noexcept
    {
      ++within;
      if (within == 8u) // 6502: the row index against 8
      {
        within = 0u;
        address = static_cast<std::uint16_t>(address + 0x140u); // 6502: &13F plus the set carry, in two bytes
      }
    };

    stepDown(); // 6502: VL3

    std::uint8_t remaining = stick.value; // negative, and the loop counts it up to zero
    do
    {
      stepDown(); // 6502: VLL2
      _canvas.ExclusiveOr(static_cast<std::uint16_t>(address + within), stickMask);
      ++remaining; // 6502: VLL2, while the count is not zero
    } while (remaining != 0u);
  }

  // ---- the compass ------------------------------------------------------------------------------

  void DrawCompassDot(Canvas& _canvas, const Compass& _compass, Picture* _picture) noexcept
  {
    if (_picture != nullptr)
    {
      DrawCompassDot2x(*_picture, _canvas, _compass.x, _compass.y, _compass.pattern);
    }

    // 6502: the compass's position and colour into the plotter's own bytes.
    // 6502: CPIX2 -- and the fall-through when the colour matches is `CPIX4`, because
    // `dot.asm` is assembled immediately in front of it.
    if (_compass.pattern == COMPASS_AHEAD)
    {
      (void)PlotBlock(_canvas, _compass.x, _compass.y, _compass.pattern);
      return;
    }

    (void)PlotDash(_canvas, _compass.x, _compass.y, _compass.pattern);
  }

  void LoadPlanetAxis(const Ship& _planet, K3Block& _axes, std::uint8_t _at) noexcept
  {
    // 6502: with the index at 0, 3 or 6, the axis's high byte.
    const SignMag24& axis = _planet.PositionAt(_at);
    _axes[_at] = axis.hi;

    // 6502: the top byte split into a magnitude and a sign, into the next two bytes.
    const std::uint8_t top = axis.sgn;
    _axes[static_cast<std::size_t>(_at) + 1u] = static_cast<std::uint8_t>(top & 0x7Fu);
    _axes[static_cast<std::size_t>(_at) + 2u] = static_cast<std::uint8_t>(top & 0x80u);
  }

  NormalisedVector NormaliseAxes(K3Block& _axes) noexcept
  {
    // 6502: the three low bytes ORed together, with a bit forced on so the loop below is
    // guaranteed to end. The tenth byte of `K3`, and this routine's own (M2-c): nothing reads it
    // after the loop.
    std::uint8_t lowBits = static_cast<std::uint8_t>(_axes[0] | _axes[3] | _axes[6] | 1u);

    // 6502: and the three high bytes together, in the accumulator.
    std::uint8_t largest = static_cast<std::uint8_t>(_axes[1] | _axes[4] | _axes[7]);

    for (;;)
    {
      // 6502: TAL2 -- one sixteen-bit doubling of the pair, and the bit that falls out of the top
      // is the signal that the largest coordinate has filled its byte.
      const ShiftResult spare = RotateLeftValue(lowBits, false);
      lowBits = spare.value;

      const ShiftResult top = RotateLeftValue(largest, spare.carry);
      largest = top.value;
      if (top.carry)
      {
        break;
      }

      // 6502: three sixteen-bit doublings, one per axis, and the last one's carry out is the
      // loop's other exit.
      ShiftResult step = RotateLeftValue(_axes[0], false);
      _axes[0] = step.value;
      step = RotateLeftValue(_axes[1], step.carry);
      _axes[1] = step.value;

      step = RotateLeftValue(_axes[3], false);
      _axes[3] = step.value;
      step = RotateLeftValue(_axes[4], step.carry);
      _axes[4] = step.value;

      step = RotateLeftValue(_axes[6], false);
      _axes[6] = step.value;
      step = RotateLeftValue(_axes[7], step.carry);
      _axes[7] = step.value;

      if (step.carry) // 6502: back to TAL2 while it does not carry
      {
        break;
      }
    }

    // 6502: TA2 -- each high byte halved and its sign folded back on, three times. Seven bits of
    // magnitude with the sign on top.
    std::array<std::uint8_t, 3> vector = {static_cast<std::uint8_t>((_axes[1] >> 1) | _axes[2]),
                                          static_cast<std::uint8_t>((_axes[4] >> 1) | _axes[5]),
                                          static_cast<std::uint8_t>((_axes[7] >> 1) | _axes[8])};

    /*
     * 6502: and there is no `RTS`. `TAS2` runs straight on into `NORM`, so the three bytes above
     * are an intermediate result and not the answer (§6.62). `Normalise` takes the span `TIDY`
     * hands it from inside a ship block, so the three bytes go through one.
     */
    const std::uint8_t length = Normalise(std::span<std::uint8_t, 3>(vector));
    return NormalisedVector{UnitVector{vector[0], vector[1], vector[2]}, length};
  }

  UnitVector LoadPlanetAxes(const Bubble& _bubble, K3Block& _axes) noexcept
  {
    // 6502: SPS3 three times, at 0, 3 and 6, all on slot 0.
    LoadPlanetAxis(_bubble.blocks[0], _axes, 0u);
    LoadPlanetAxis(_bubble.blocks[0], _axes, 3u);
    LoadPlanetAxis(_bubble.blocks[0], _axes, 6u);

    return NormaliseAxes(_axes).vector; // 6502: the fall-through into TAS2
  }

  UnitVector LoadStationAxes(const Bubble& _bubble, K3Block& _axes) noexcept
  {
    // 6502: SPL1 -- nine bytes copied downwards: the station's position, in the block's own order.
    const std::array<std::uint8_t, SHIP_BLOCK_SIZE> station = _bubble.blocks[1].ToBytes();
    for (std::size_t at = 9u; at-- > 0u;)
    {
      _axes[at] = station[at];
    }

    return NormaliseAxes(_axes).vector; // 6502: TAS2, as a tail call
  }

  CompassOffset ScaleToCompass(std::uint8_t _value) noexcept
  {
    // 6502: the magnitude doubled into X, and the sign bit caught in the carry and rotated back
    // down into Y as 0 or 128.
    const ShiftResult doubled = RotateLeftValue(_value, false);
    const std::uint8_t sign = RotateRight(0u, doubled.carry).value;

    constexpr std::uint8_t COMPASS_RADIUS = 20u; // 6502: 20 into Q -- the compass's radius in pixels
    const ScaledDivision divided = DivideAndScale(doubled.value, COMPASS_RADIUS);

    const std::uint8_t whole = divided.whole; // 6502: out of P

    // 6502: LL163 -- negate the offset for a negative coordinate, and hand back 255 as
    // the sign so that `SP2` can hold it in a byte.
    if ((sign & 0x80u) != 0u)
    {
      const std::uint8_t negated = static_cast<std::uint8_t>(static_cast<std::uint8_t>(whole ^ 0xFFu) + 1u);
      return {negated, 0xFFu, divided.carry};
    }

    return {whole, 0u, divided.carry}; // 6502: a zero sign
  }

  void DrawCompass(Canvas& _canvas, Compass& _compass, UnitVector _towards, Picture* _picture) noexcept
  {
    /*
     * 6502: `SPS2` on the x axis, then 195 added to what it left in X.
     *
     * That addition has no clear of the carry, and what it adds is `DVID4`'s exit carry from inside
     * `SPS2` -- the
     * thirteenth flag this port had to go back for (§6.60). The same again below, as an `SBC`
     * with no `SEC`, which is why 156 comes out as 155 for every input the divide does not
     * saturate on.
     */
    const CompassOffset across = ScaleToCompass(_towards.x);
    _compass.x = AddWithCarry(across.offset, 195u, across.carry).value;

    const CompassOffset down = ScaleToCompass(_towards.y);
    // 6502: the original parks `ScaleToCompass`'s X in `T` before it can subtract from a literal;
    // the port subtracts the value where it stands.
    _compass.y = SubtractWithCarry(156u, down.offset, down.carry).value;

    // 6502: yellow ahead and green behind, chosen on the sign of the z axis.
    _compass.pattern = ((_towards.z & 0x80u) != 0u) ? COMPASS_BEHIND : COMPASS_AHEAD;

    DrawCompassDot(_canvas, _compass, _picture); // 6502: DOT, as a tail call
  }

  void AimCompassAtStation(Canvas& _canvas, Compass& _compass, const Bubble& _bubble, K3Block& _axes, Picture* _picture) noexcept
  {
    const UnitVector towards = LoadStationAxes(_bubble, _axes); // 6502: SPS4
    DrawCompass(_canvas, _compass, towards, _picture);          // 6502: the fall-through into SP2
  }

  void UpdateCompass(Canvas& _canvas, Compass& _compass, const Bubble& _bubble, Picture* _picture) noexcept
  {
    DrawCompassDot(_canvas, _compass, _picture); // 6502: DOT -- draw the old dot again to erase it

    K3Block axes{};

    // 6502: SP1 -- and `SSPR` is the station's entry in `MANY` (§6.58).
    if (_bubble.StationPresent() != 0u)
    {
      AimCompassAtStation(_canvas, _compass, _bubble, axes, _picture);
      return;
    }

    const UnitVector towards = LoadPlanetAxes(_bubble, axes); // 6502: SPS1
    DrawCompass(_canvas, _compass, towards, _picture);        // 6502: SP2, as a tail call
  }

} // namespace Elite
