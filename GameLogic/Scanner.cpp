#include "pch.h"

#include "Scanner.h"

#include "Arith.h"
#include "EliteTypes.h"
#include "LookupTables.h"

#include <span>

namespace Elite
{

  // ---- the scanner ------------------------------------------------------------------------------

  void DrawScannerBlip(Canvas& _canvas, DrawWorkspace& _work, const ShipBlock& _ship, std::uint8_t _type, std::uint8_t _view) noexcept
  {
    // 6502: LDA QQ11 / BNE SCR1 -- no dashboard on any view but the space view, so no scanner.
    if (_view != 0u)
    {
      return;
    }

    // 6502: LDA INWK+31 / AND #%00010000 / BEQ SCR1 -- bit 4 is "show this on the scanner", and
    // it is cleared for the ships that have no blip at all.
    if ((_ship[31] & 0x10u) == 0u)
    {
      return;
    }

    // 6502: LDX TYPE / BMI SCR1 -- the planet and the sun are types 128 and 129 and are not shown.
    if ((_type & 0x80u) != 0u)
    {
      return;
    }

    // The index is a ship TYPE, 1 to `SHIP_TYPE_COUNT`, which is what the table is sized by and
    // what `FRIN` can hold. The `BMI` above has already taken the planet and the sun out of it.
    _work.col = SCANNER_COLOUR_TABLE[_type];

    /*
     * 6502: LDA INWK+1 / ORA INWK+4 / ORA INWK+7 / AND #%11000000 / BNE SCR1.
     *
     * Off the scanner if ANY of the three high bytes reaches 64, which is the one range check the
     * routine makes -- everything below works in the top six bits of a coordinate and would wrap
     * rather than clip. It is a test of the three ORed together and not of each in turn, which is
     * the same answer for a check of "is any bit 6 or 7 set anywhere".
     */
    if (((_ship[1] | _ship[4] | _ship[7]) & 0xC0u) != 0u)
    {
      return;
    }

    /*
     * 6502: the x coordinate, and the negation is the ordinary sign-magnitude one -- except that
     * the `CLC` is before the branch, so the positive path adds 123 with a clear carry and the
     * negative path adds it with whatever `ADC #1` produced. A ship at x_hi = 0 with the sign bit
     * set therefore lands one pixel right of one at x_hi = 0 without it: 124 rather than 123.
     */
    bool carry = false; // 6502: CLC
    std::uint8_t across = _ship[1];
    if ((_ship[2] & 0x80u) != 0u) // 6502: LDX INWK+2 / BPL SC2
    {
      const AddResult negated = AddWithCarry(static_cast<std::uint8_t>(across ^ 0xFFu), 1u, carry);
      across = negated.value;
      carry = negated.carry;
    }

    _work.x1 = AddWithCarry(across, 123u, carry).value; // 6502: SC2 -- ADC #123 / STA X1

    /*
     * 6502: the scanner's ellipse is drawn around a horizontal line, and this is where on that
     * line the ship's DEPTH puts it -- z_hi / 4, and then the whole thing inverted, because screen
     * rows count downwards and z counts away from the player.
     *
     * `SC` is the byte it goes in and it is NOT the screen pointer here. It becomes one further
     * down, when `CPIX4` overwrites it; until then it is the row the stick is drawn back to.
     */
    std::uint8_t depth = static_cast<std::uint8_t>(_ship[7] >> 2);
    bool depthCarry = false;      // 6502: CLC
    if ((_ship[8] & 0x80u) != 0u) // 6502: LDX INWK+8 / BPL SC3
    {
      depth = static_cast<std::uint8_t>(depth ^ 0xFFu);
      depthCarry = true; // 6502: SEC -- so the one's complement and the carry make a negation
    }

    const std::uint8_t ground = static_cast<std::uint8_t>(AddWithCarry(depth, 83u, depthCarry).value ^ 0xFFu);

    /*
     * 6502: and the height above that line, which is y_hi / 2 -- with the test the other way up.
     *
     * `BMI SCD6` skips the negation for a NEGATIVE y, so it is a positive y that gets complemented.
     * That is not an inconsistency with the depth above: screen rows increase downwards, so a ship
     * above the plane of flight has to move to a smaller row number.
     */
    std::uint8_t height = static_cast<std::uint8_t>(_ship[4] >> 1);
    bool heightCarry = false;     // 6502: CLC
    if ((_ship[5] & 0x80u) == 0u) // 6502: LDX INWK+5 / BMI SCD6
    {
      height = static_cast<std::uint8_t>(height ^ 0xFFu);
      heightCarry = true; // 6502: SEC
    }

    std::uint8_t row = AddWithCarry(height, ground, heightCarry).value; // 6502: SCD6 -- ADC SC

    // 6502: CMP #146 / BCS / LDA #146 / CMP #199 / BCC / LDA #198 -- clamped into the dashboard,
    // and the second comparison sees the clamped value, which is why the two cannot both fire.
    if (row < 146u)
    {
      row = 146u;
    }
    else if (row >= 199u)
    {
      row = 198u;
    }

    _work.y1 = row;

    // 6502: SEC / SBC SC / PHP -- how tall the stick is, and which way up. The carry is the sign
    // and it survives `CPIX4` on the stack, because `TAX` below sets N and Z but not C.
    const SubResult stick = SubtractWithCarry(row, ground, true);

    const CellCursor cursor = PlotBlock(_canvas, _work); // 6502: JSR CPIX4

    /*
     * 6502: LDA CTWOS2+2,X / AND COL / STA X1.
     *
     * The stick has the same pixel pattern as the dot's RIGHT-hand pixel, so it comes out of the
     * right side of the dot rather than the middle -- and `X1` stops being a coordinate here and
     * becomes that pattern. The cursor's cell has already followed the same wrap, so the two agree
     * about which character block the stick belongs in.
     */
    _work.x1 = static_cast<std::uint8_t>(MULTICOLOUR_MASK_TABLE[cursor.pixel + 2u] & _work.col);

    // 6502: TAX / BEQ RTS -- a ship exactly on the plane of flight has a dot and no stick.
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
          address = static_cast<std::uint16_t>(address - 0x140u); // 6502: SEC / SBC #&40 / SBC #&01
        }
        else
        {
          --within;
        }

        _canvas.ExclusiveOr(static_cast<std::uint16_t>(address + within), _work.x1);
        --remaining; // 6502: DEX / BNE VLL1
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
     * The carry into the `ADC #&3F` is always set, because the only way to reach it is through a
     * `CPY #8` that found Y equal to 8.
     */
    auto stepDown = [&address, &within]() noexcept
    {
      ++within;
      if (within == 8u) // 6502: CPY #8 / BNE
      {
        within = 0u;
        address = static_cast<std::uint16_t>(address + 0x140u); // 6502: ADC #&3F / ADC #1, C set
      }
    };

    stepDown(); // 6502: VL3

    std::uint8_t remaining = stick.value; // negative, and the loop counts it up to zero
    do
    {
      stepDown(); // 6502: VLL2
      _canvas.ExclusiveOr(static_cast<std::uint16_t>(address + within), _work.x1);
      ++remaining; // 6502: INX / BNE VLL2
    } while (remaining != 0u);
  }

  // ---- the compass ------------------------------------------------------------------------------

  void DrawCompassDot(Canvas& _canvas, DrawWorkspace& _work, const Compass& _compass) noexcept
  {
    _work.y1 = _compass.y;
    _work.x1 = _compass.x;
    _work.col = _compass.colour;

    // 6502: CMP #YELLOW / BNE CPIX2 -- and the fall-through when it matches is `CPIX4`, because
    // `dot.asm` is assembled immediately in front of it.
    if (_compass.colour == COMPASS_AHEAD)
    {
      (void)PlotBlock(_canvas, _work);
      return;
    }

    (void)PlotDash(_canvas, _work);
  }

  void LoadPlanetAxis(const ShipBlock& _planet, K3Block& _axes, std::uint8_t _at) noexcept
  {
    // 6502: LDA K%+1,X / STA K3,X
    _axes[_at] = _planet[static_cast<std::size_t>(_at) + 1u];

    // 6502: LDA K%+2,X / TAY / AND #%01111111 / STA K3+1,X / TYA / AND #%10000000 / STA K3+2,X
    const std::uint8_t top = _planet[static_cast<std::size_t>(_at) + 2u];
    _axes[static_cast<std::size_t>(_at) + 1u] = static_cast<std::uint8_t>(top & 0x7Fu);
    _axes[static_cast<std::size_t>(_at) + 2u] = static_cast<std::uint8_t>(top & 0x80u);
  }

  void NormaliseAxes(K3Block& _axes, DrawWorkspace& _work, MathWorkspace& _math) noexcept
  {
    // 6502: LDA K3 / ORA K3+3 / ORA K3+6 / ORA #1 / STA K3+9 -- the low bytes together, with a bit
    // forced on so the loop below is guaranteed to end.
    _axes[9] = static_cast<std::uint8_t>(_axes[0] | _axes[3] | _axes[6] | 1u);

    // 6502: LDA K3+1 / ORA K3+4 / ORA K3+7 -- and the high bytes together, in A.
    std::uint8_t largest = static_cast<std::uint8_t>(_axes[1] | _axes[4] | _axes[7]);

    for (;;)
    {
      // 6502: TAL2 -- ASL K3+9 / ROL A / BCS TA2. One sixteen-bit shift of (A K3+9), and the bit
      // that falls out of the top is the signal that the largest coordinate has filled its byte.
      const ShiftResult spare = RotateLeftValue(_axes[9], false);
      _axes[9] = spare.value;

      const ShiftResult top = RotateLeftValue(largest, spare.carry);
      largest = top.value;
      if (top.carry)
      {
        break;
      }

      // 6502: ASL K3 / ROL K3+1 / ASL K3+3 / ROL K3+4 / ASL K3+6 / ROL K3+7 -- three sixteen-bit
      // doublings, and the last one's carry out is the loop's other exit.
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

      if (step.carry) // 6502: BCC TAL2
      {
        break;
      }
    }

    // 6502: TA2 -- LDA K3+1 / LSR A / ORA K3+2 / STA XX15, three times. Seven bits of magnitude
    // with the sign back on top.
    _work.x1 = static_cast<std::uint8_t>((_axes[1] >> 1) | _axes[2]);
    _work.y1 = static_cast<std::uint8_t>((_axes[4] >> 1) | _axes[5]);
    _work.x2 = static_cast<std::uint8_t>((_axes[7] >> 1) | _axes[8]);

    /*
     * 6502: and there is no `RTS`. `TAS2` runs straight on into `NORM`, so the three bytes above
     * are an intermediate result and not the answer (§6.62).
     *
     * The copy is because `XX15` is three fields rather than an array -- `X1`, `Y1` and `X2`, which
     * is what `XX15` is (§6.37) -- and `Normalise` takes the span `TIDY` hands it from inside a
     * ship block. Three bytes out and three back is cheaper than making the whole workspace an
     * array for one caller.
     */
    std::array<std::uint8_t, 3> vector = {_work.x1, _work.y1, _work.x2};
    Normalise(_math, std::span<std::uint8_t, 3>(vector));
    _work.x1 = vector[0];
    _work.y1 = vector[1];
    _work.x2 = vector[2];
  }

  void LoadPlanetAxes(const Bubble& _bubble, K3Block& _axes, DrawWorkspace& _work, MathWorkspace& _math) noexcept
  {
    // 6502: LDX #0 / JSR SPS3 / LDX #3 / JSR SPS3 / LDX #6 / JSR SPS3, all on slot 0.
    LoadPlanetAxis(_bubble.blocks[0], _axes, 0u);
    LoadPlanetAxis(_bubble.blocks[0], _axes, 3u);
    LoadPlanetAxis(_bubble.blocks[0], _axes, 6u);

    NormaliseAxes(_axes, _work, _math); // 6502: the fall-through into TAS2
  }

  void LoadStationAxes(const Bubble& _bubble, K3Block& _axes, DrawWorkspace& _work, MathWorkspace& _math) noexcept
  {
    // 6502: LDX #8 / SPL1 -- LDA K%+NI%,X / STA K3,X / DEX / BPL SPL1. Nine bytes, downwards.
    for (std::size_t at = 9u; at-- > 0u;)
    {
      _axes[at] = _bubble.blocks[1][at];
    }

    NormaliseAxes(_axes, _work, _math); // 6502: JMP TAS2
  }

  CompassOffset ScaleToCompass(MathWorkspace& _math, std::uint8_t _a) noexcept
  {
    // 6502: ASL A / TAX / LDA #0 / ROR A / TAY -- the magnitude doubled into X, and the sign bit
    // caught in the carry and rotated back down into Y as 0 or 128.
    const ShiftResult doubled = RotateLeftValue(_a, false);
    const std::uint8_t sign = RotateRight(0u, doubled.carry).value;

    _math.q = 20u; // 6502: LDA #20 / STA Q -- the compass's radius in pixels
    const ScaledDivision divided = DivideAndScale(_math, doubled.value);

    const std::uint8_t whole = _math.p; // 6502: LDX P

    // 6502: TYA / BMI LL163 -- negate the offset for a negative coordinate, and hand back 255 as
    // the sign so that `SP2` can hold it in a byte.
    if ((sign & 0x80u) != 0u)
    {
      const std::uint8_t negated = static_cast<std::uint8_t>(static_cast<std::uint8_t>(whole ^ 0xFFu) + 1u);
      return {negated, 0xFFu, divided.carry};
    }

    return {whole, 0u, divided.carry}; // 6502: LDY #0
  }

  void DrawCompass(Canvas& _canvas, DrawWorkspace& _work, MathWorkspace& _math, Compass& _compass) noexcept
  {
    /*
     * 6502: LDA XX15 / JSR SPS2 / TXA / ADC #195 / STA COMX.
     *
     * `ADC` with no `CLC`, and what it adds is `DVID4`'s exit carry from inside `SPS2` -- the
     * thirteenth flag this port had to go back for (§6.60). The same again below, as an `SBC`
     * with no `SEC`, which is why 156 comes out as 155 for every input the divide does not
     * saturate on.
     */
    const CompassOffset across = ScaleToCompass(_math, _work.x1);
    _compass.x = AddWithCarry(across.offset, 195u, across.carry).value;

    const CompassOffset down = ScaleToCompass(_math, _work.y1);
    _math.t = down.offset; // 6502: STX T
    _compass.y = SubtractWithCarry(156u, _math.t, down.carry).value;

    // 6502: LDA #YELLOW / LDX XX15+2 / BPL P%+4 / LDA #GREEN / STA COMC.
    _compass.colour = ((_work.x2 & 0x80u) != 0u) ? COMPASS_BEHIND : COMPASS_AHEAD;

    DrawCompassDot(_canvas, _work, _compass); // 6502: JMP DOT
  }

  void AimCompassAtStation(Canvas& _canvas, DrawWorkspace& _work, MathWorkspace& _math, Compass& _compass, const Bubble& _bubble,
                           K3Block& _axes) noexcept
  {
    LoadStationAxes(_bubble, _axes, _work, _math); // 6502: JSR SPS4
    DrawCompass(_canvas, _work, _math, _compass);  // 6502: the fall-through into SP2
  }

  void UpdateCompass(Canvas& _canvas, DrawWorkspace& _work, MathWorkspace& _math, Compass& _compass, const Bubble& _bubble) noexcept
  {
    DrawCompassDot(_canvas, _work, _compass); // 6502: JSR DOT -- draw the old dot again to erase it

    K3Block axes{};

    // 6502: LDA SSPR / BNE SP1 -- and `SSPR` is the station's entry in `MANY` (§6.58).
    if (_bubble.StationPresent() != 0u)
    {
      AimCompassAtStation(_canvas, _work, _math, _compass, _bubble, axes);
      return;
    }

    LoadPlanetAxes(_bubble, axes, _work, _math);  // 6502: JSR SPS1
    DrawCompass(_canvas, _work, _math, _compass); // 6502: JMP SP2
  }

} // namespace Elite
