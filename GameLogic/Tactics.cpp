#include "Tactics.h"

#include "ShipMove.h"

namespace Elite
{

  namespace
  {

    /*
     * 6502: TAS7 -- one axis of `DCS1`'s subtraction, entered with A = the nose vector's high byte
     * and X = the base index into `K3`.
     *
     * It is not a subroutine of its own in the source: `DCS1` falls into it from the third `LDX`,
     * which is why the routine's last call is a fall-through rather than a `JSR`.
     */
    void OffsetAxis(K3Block& _axes, std::uint8_t _nose, std::uint8_t _at) noexcept
    {
      // 6502: ASL A / STA R -- the doubling, and the carry out is the vector's SIGN.
      const std::uint8_t doubled = static_cast<std::uint8_t>(_nose << 1u);
      const bool negative = (_nose & 0x80u) != 0u;

      /*
       * 6502: LDA #0 / ROR A / EOR #%10000000 / EOR K3+2,X / BMI TS71.
       *
       * The `ROR` puts the carry into bit 7 of a zero, so this is the vector's sign on its own; the
       * `EOR` flips it, because what is being added is the NEGATED vector. Comparing that against
       * `K3`'s sign says whether the magnitudes add or fight.
       */
      const std::uint8_t sign = static_cast<std::uint8_t>((negative ? 0x80u : 0x00u) ^ 0x80u);
      const bool opposed = ((sign ^ _axes[_at + 2u]) & 0x80u) != 0u;

      if (!opposed)
      {
        /*
         * 6502: LDA R / ADC K3,X / STA K3,X / BCC TS72 / INC K3+1,X.
         *
         * The carry into the `ADC` is zero and no `CLC` says so: the `ROR A` four instructions up
         * shifted bit 0 of a literal zero out into it. The sign byte is not touched on this path.
         */
        const AddResult low = AddWithCarry(doubled, _axes[_at], false);
        _axes[_at] = low.value;

        if (low.carry)
        {
          ++_axes[_at + 1u]; // 6502: INC K3+1,X -- an INC, so it cannot carry any further
        }
        return;
      }

      // 6502: .TS71 LDA K3,X / SEC / SBC R / STA K3,X / LDA K3+1,X / SBC #0 / STA K3+1,X.
      SubResult low = SubtractWithCarry(_axes[_at], doubled, true);
      _axes[_at] = low.value;

      SubResult high = SubtractWithCarry(_axes[_at + 1u], 0u, low.carry);
      _axes[_at + 1u] = high.value;

      if (high.carry)
      {
        return; // 6502: BCS TS72 -- the magnitude was big enough, so the sign stands
      }

      /*
       * 6502: EOR #%11111111 / ADC #1 twice, then EOR #%10000000 on the sign byte.
       *
       * The subtraction went the wrong way round, so the answer is negated and the sign flipped --
       * the same fix `MVT3` makes, and the `ADC #1` gets its clean carry from the borrow the `BCS`
       * just tested.
       */
      AddResult negatedLow = AddWithCarry(static_cast<std::uint8_t>(_axes[_at] ^ 0xFFu), 1u, false);
      _axes[_at] = negatedLow.value;

      AddResult negatedHigh = AddWithCarry(static_cast<std::uint8_t>(_axes[_at + 1u] ^ 0xFFu), 0u, negatedLow.carry);
      _axes[_at + 1u] = negatedHigh.value;

      _axes[_at + 2u] = static_cast<std::uint8_t>(_axes[_at + 2u] ^ 0x80u);
    }

  } // namespace

  void SubtractShipAxis(const ShipBlock& _other, const ShipBlock& _work, K3Block& _axes, MathWorkspace& _math,
                        std::uint8_t _at) noexcept
  {
    // 6502: LDA (V),Y / EOR #%10000000 / STA K+3 -- the other object's sign, negated.
    _math.k[3] = static_cast<std::uint8_t>(_other[_at + 2u] ^ 0x80u);

    // 6502: DEY / LDA (V),Y / STA K+2 / DEY / LDA (V),Y / STA K+1.
    _math.k[2] = _other[_at + 1u];
    _math.k[1] = _other[_at];

    // 6502: STY U / LDX U / JSR MVT3 -- K = K + INWK+X, so K is now this ship minus the other.
    _math.u = _at;
    AddShipCoordinateToK(_work, _math, _at);

    // 6502: STA K3+2,X, and the A it stores is the sign byte `MVT3` left in the register.
    _axes[_at + 2u] = _math.k[3];
    _axes[_at + 1u] = _math.k[2];
    _axes[_at] = _math.k[1];
  }

  void SubtractShipAxes(const ShipBlock& _other, const ShipBlock& _work, K3Block& _axes, MathWorkspace& _math) noexcept
  {
    // 6502: LDY #2 / JSR TAS1 / LDY #5 / JSR TAS1 / LDY #8, and the last one is a fall-through.
    SubtractShipAxis(_other, _work, _axes, _math, 0u);
    SubtractShipAxis(_other, _work, _axes, _math, 3u);
    SubtractShipAxis(_other, _work, _axes, _math, 6u);
  }

  void SubtractStationAxes(const Bubble& _bubble, const ShipBlock& _work, K3Block& _axes, MathWorkspace& _math) noexcept
  {
    // 6502: LDA #LO(K%+NI%) / STA V / LDA #HI(K%+NI%), and then straight into `VCSUB`.
    SubtractShipAxes(_bubble.blocks[1], _work, _axes, _math);
  }

  AddSignedResult DotProductWithShip(const ShipBlock& _block, const DrawWorkspace& _draw, MathWorkspace& _math,
                                     std::uint8_t _at) noexcept
  {
    // 6502: LDX INWK,Y / STX Q / LDA XX15 / JSR MULT12 -- (S R) = vect_x * XX15.
    _math.q = _block[_at];
    MultiplySignedToSR(_math, _draw.x1);

    // 6502: LDX INWK+2,Y / STX Q / LDA XX15+1 / JSR MAD / STA S / STX R.
    _math.q = _block[_at + 2u];
    const AddSignedResult second = MultiplyAndAdd(_math, _draw.y1);
    _math.s = second.high;
    _math.r = second.low;

    // 6502: LDX INWK+4,Y / STX Q / LDA XX15+2, and no `JSR` -- it falls into `MAD`.
    _math.q = _block[_at + 4u];
    return MultiplyAndAdd(_math, _draw.x2);
  }

  void NegateVector(DrawWorkspace& _draw) noexcept
  {
    // 6502: three EOR #%10000000s over XX15, XX15+1 and XX15+2.
    _draw.x1 = static_cast<std::uint8_t>(_draw.x1 ^ 0x80u);
    _draw.y1 = static_cast<std::uint8_t>(_draw.y1 ^ 0x80u);
    _draw.x2 = static_cast<std::uint8_t>(_draw.x2 ^ 0x80u);
  }

  void OffsetDockingPosition(const Bubble& _bubble, K3Block& _axes) noexcept
  {
    const ShipBlock& station = _bubble.blocks[1];

    // 6502: JSR P%+3 -- the body twice, so each subtraction is the nose vector times four.
    for (int pass = 0; pass < 2; ++pass)
    {
      OffsetAxis(_axes, station[NOSE_VECTOR_X], 0u); // 6502: LDA K%+NI%+10 / LDX #0 / JSR TAS7
      OffsetAxis(_axes, station[NOSE_VECTOR_Y], 3u); // 6502: LDA K%+NI%+12 / LDX #3 / JSR TAS7
      OffsetAxis(_axes, station[NOSE_VECTOR_Z], 6u); // 6502: LDA K%+NI%+14 / LDX #6, a fall-through
    }
  }

} // namespace Elite
