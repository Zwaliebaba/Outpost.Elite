#include "pch.h"

#include "Explosion.h"

#include "EliteTypes.h"

#include <array>
#include <cstdint>

/*
 * The explosion cloud (slice 4b-b).
 *
 * 6502: DOEXP, PTCLS, PTCLS2 and EXS1. Three hundred and eleven instructions between them, and
 * almost all of the difficulty is in flags: five of the routine's decisions are carried into an
 * `ADC` or an `SBC` that has no `CLC` or `SEC` in front of it, so the arithmetic depends on a
 * comparison made several instructions earlier. Each one is named where it happens.
 */
namespace Elite
{

  namespace
  {
    /*
     * 6502: PTCLS and PTCLS2, which are one body -- see the header. `_effects` is null for `PTCLS`
     * and the seam for `PTCLS2`.
     */
    void DrawParticles(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, Rng& _rng, const ShipBlock& _work, LineHeap& _heap,
                       const Bubble& _bubble, ExplosionEffects* _effects) noexcept
    {
      const std::uint16_t address = ShipHeapAddress(_work);

      /*
       * 6502: sprx and spry -- where the burst sits relative to the cloud's centre.
       *
       * They are RAM in the original and locals here, because `PTCLS2` writes them at the top and
       * reads them further down the same call; nothing else in the game touches either byte.
       */
      std::uint8_t sprx = 0;
      std::uint8_t spry = 0;

      if (_effects != nullptr)
      {
        _effects->SetRasterMode(0x05u); // 6502: LDA #%101 / JSR SETL1 -- map the I/O page in

        // 6502: LDA INWK+7 / CMP #7 -- the compare is made with A already loaded for the register
        // write, so the two answers are chosen before the branch rather than after it.
        const bool distant = _work[SHIP_Z_OFFSET + 1] >= 7u;
        _effects->SetSpriteExpansion(distant ? 0xFDu : 0xFFu);
        sprx = distant ? 44u : 32u;
        spry = distant ? 40u : 30u;
      }

      _math.q = _heap.Read(address); // 6502: byte 0 of the heap -- this frame's cloud size

      /*
       * 6502: byte 1, the cloud counter, turned into a particle count.
       *
       * `BPL P%+4 / EOR #&FF` is what makes the cloud grow and then shrink: past 128 the counter
       * is complemented, so the count walks 1..7 up and 7..1 back down over the explosion's life.
       * Four shifts rather than the cassette version's three -- the C64 draws half as many.
       */
      std::uint8_t counter = _heap.Read(static_cast<std::uint16_t>(address + 1u));
      if ((counter & 0x80u) != 0u)
      {
        counter ^= 0xFFu;
      }
      _math.u = static_cast<std::uint8_t>((counter >> 4) | 1u); // 6502: LSR A x4 / ORA #1 / STA U
      _math.tgt = _heap.Read(static_cast<std::uint16_t>(address + 2u));

      // 6502: LDA RAND+1 / PHA -- kept across the whole routine, because everything below
      // deliberately destroys the generator's state and one byte of it has to survive.
      const std::uint8_t stacked = _rng.State()[1];

      std::uint8_t vertex = 6; // 6502: LDY #6, the byte before the first vertex on the heap

      do
      {
        /*
         * 6502: EXL3 -- four bytes off the heap into `K3`, BACKWARDS, so that
         * `K3+3 = x_lo`, `K3+2 = x_hi`, `K3+1 = y_lo`, `K3+0 = y_hi`.
         *
         * These four are `K3` in the original and therefore also `XX2+0` to `XX2+3`, the first
         * four face flags -- the aliasing `GeometryWorkspace::xx2` already warns about. They are
         * locals here: `DOEXP` is the last thing that happens to a ship in a frame, and every
         * reader of `XX2`, `K3` and `K4` writes them before it reads them.
         */
        std::array<std::uint8_t, 4> k3{};
        for (int index = 3; index >= 0; --index)
        {
          ++vertex;
          k3[static_cast<std::size_t>(index)] = _heap.Read(static_cast<std::uint16_t>(address + vertex));
        }
        _math.cnt = vertex; // 6502: STY CNT

        if (_effects != nullptr)
        {
          /*
           * 6502: the burst sprite, placed at the vertex plus the offset chosen above.
           *
           * Both coordinates are sixteen-bit adds with an explicit `CLC`, and both are rejected on
           * the HIGH byte first: negative or 512 and over in x, anything at all in y. The x test is
           * two branches because a sprite's x really is nine bits wide, and the y test is one
           * because a screen row is not.
           */
          const AddResult lowX = AddWithCarry(k3[3], sprx, false);
          const AddResult highX = AddWithCarry(k3[2], 0, lowX.carry);

          if ((highX.value & 0x80u) == 0u && highX.value < 2u)
          {
            const AddResult lowY = AddWithCarry(k3[1], spry, false);
            const AddResult highY = AddWithCarry(k3[0], 0, lowY.carry);

            if (highY.value == 0u && lowY.value < EXPLOSION_SPRITE_BOTTOM)
            {
              _effects->ShowExplosionSprite(static_cast<std::uint16_t>(lowX.value | (highX.value << 8)), lowY.value);
            }
          }
        }

        /*
         * 6502: EXL2 -- bytes 3 to 6 of the heap, EORed with the vertex index, become the four
         * generator seeds. `STA &FFFF,Y` with Y from 3 to 6 wraps to &0002, which is `RAND`.
         *
         * This is the whole trick of the routine: the cloud is regenerated from these four bytes
         * every frame, so drawing it twice erases it, and EORing with the index is what stops all
         * of a ship's vertices blooming the same cloud.
         */
        std::array<std::uint8_t, 4> seeds{};
        for (std::size_t byte = 0; byte < 4u; ++byte)
        {
          seeds[byte] = static_cast<std::uint8_t>(_heap.Read(static_cast<std::uint16_t>(address + 3u + byte)) ^ _math.cnt);
        }
        _rng.SetState(seeds);

        // 6502: LDY U / EXL4 ... DEY / BPL EXL4 -- so the body runs U + 1 times, not U.
        std::uint8_t particle = _math.u;
        for (;;)
        {
          // 6502: JSR DORND2 / STA ZZ -- how far away the particle is, which is what decides
          // whether `PIXEL` draws one mark, two, or a square.
          _draw.zz = _rng.NextRepeatable().value;

          _math.r = k3[1];
          const ExplosionOffset offsetY = OffsetByCloud(_math, _rng, k3[0]);

          if (offsetY.high != 0u || offsetY.low >= EXPLOSION_PARTICLE_BOTTOM)
          {
            /*
             * 6502: EX11 -- and it is not a bare `JMP EX4`. It runs the generator once MORE before
             * rejoining, so that a particle rejected on its y costs the same two random numbers as
             * one that got as far as its x. Without it the cloud would not repeat and could not be
             * erased.
             */
            static_cast<void>(_rng.NextRepeatable());
          }
          else
          {
            _draw.y1 = offsetY.low; // 6502: STX Y1

            _math.r = k3[3];
            const ExplosionOffset offsetX = OffsetByCloud(_math, _rng, k3[2]);

            if (offsetX.high == 0u)
            {
              PlotPixel(_canvas, _draw, offsetX.low, _draw.y1);
            }
          }

          --particle;
          if ((particle & 0x80u) != 0u)
          {
            break;
          }
        }

        vertex = _math.cnt; // 6502: LDY CNT
      } while (vertex < _math.tgt);

      /*
       * 6502: PLA / STA RAND+1, then LDA K%+6 / STA RAND+3.
       *
       * Three fates for four bytes: `RAND+1` comes back off the stack, `RAND+3` is replaced by the
       * PLANET's z_lo -- byte 6 of slot 0, the same "pretty random" byte the spawner reads -- and
       * `RAND` and `RAND+2` keep whatever the last particle left. The next `DORND` anywhere in the
       * game runs on that mixture, so it is part of the routine's answer and not tidying up.
       */
      std::array<std::uint8_t, 4> state = _rng.State();
      state[1] = stacked;

      if (_effects != nullptr)
      {
        _effects->SetRasterMode(0x04u); // 6502: LDA #%100 / JSR SETL1 -- map the I/O page back out
      }

      state[3] = _bubble.blocks[0][6];
      _rng.SetState(state);
    }
  } // namespace

  ExplosionOffset OffsetByCloud(MathWorkspace& _math, Rng& _rng, std::uint8_t _a) noexcept
  {
    _math.s = _a; // 6502: STA S -- the high byte of the vertex, kept for the tail

    // 6502: the inlined copy of DORND2 -- the C64 spells the routine out here rather than calling
    // it, which changes the timing and nothing else.
    const RngResult random = _rng.NextRepeatable();
    const ShiftResult doubled = RotateLeftValue(random.value, random.carry);

    /*
     * 6502: FMLTU's own `STX P`, and this is the one call site in the game where the port can say
     * what X held -- `LDA RAND+1 / TAX` two instructions back, so it is the generator's PREVIOUS
     * byte. `FMLTU` parks X there to preserve it and every exit reloads it, which leaves `P`
     * holding a register value nothing goes on to read. See `MultiplyByLog` in `Arith.h`.
     */
    _math.p = random.previous;

    if (doubled.carry)
    {
      // 6502: EX5 -- the negative half. The carry is SET here BECAUSE the branch was taken, and
      // `FMLTU` passes an entry carry straight through on its two zero exits, so it matters.
      const WideResult product = MultiplyByLog(_math, doubled.value, true);
      _math.t = product.high;

      // 6502: LDA R / SBC T / TAX / LDA S / SBC #0 -- and the borrow going in is whatever `FMLTU`
      // left, not a `SEC`.
      const SubResult low = SubtractWithCarry(_math.r, _math.t, product.carry);
      const SubResult high = SubtractWithCarry(_math.s, 0, low.carry);
      return ExplosionOffset{high.value, low.value};
    }

    // 6502: JSR FMLTU / ADC R / TAX / LDA S / ADC #0 -- the positive half, and the same borrowed
    // carry the other way round. §6.42 recorded this call as one of the two that read it.
    const WideResult product = MultiplyByLog(_math, doubled.value, false);
    const AddResult low = AddWithCarry(product.high, _math.r, product.carry);
    const AddResult high = AddWithCarry(_math.s, 0, low.carry);
    return ExplosionOffset{high.value, low.value};
  }

  void DrawExplosionParticles(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, Rng& _rng, const ShipBlock& _work,
                              LineHeap& _heap, const Bubble& _bubble) noexcept
  {
    DrawParticles(_canvas, _draw, _math, _rng, _work, _heap, _bubble, nullptr);
  }

  void DrawExplosionParticlesWithSprite(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, Rng& _rng, const ShipBlock& _work,
                                        LineHeap& _heap, const Bubble& _bubble, ExplosionEffects& _effects) noexcept
  {
    DrawParticles(_canvas, _draw, _math, _rng, _work, _heap, _bubble, &_effects);
  }

  void DrawExplosionCloud(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, Rng& _rng, ShipBlock& _work, LineHeap& _heap,
                          const GeometryWorkspace& _geometry, const Bubble& _bubble, ExplosionEffects& _effects) noexcept
  {
    const std::uint16_t address = ShipHeapAddress(_work);

    // 6502: bit 6 of byte 31 -- there is a cloud on the screen from last frame, so draw it again
    // to rub it out. Always through `PTCLS`; the burst sprite is placed once and left alone.
    if ((_work[SHIP_STATE_OFFSET] & SHIP_STATE_CLOUD_DRAWN) != 0u)
    {
      DrawParticles(_canvas, _draw, _math, _rng, _work, _heap, _bubble, nullptr);
    }

    /*
     * 6502: (A T) = z, scaled into one byte -- and the CARRY IT LEAVES IS THE POINT.
     *
     * `CMP #32` decides whether the ship is far enough away to cap the distance at 254, and
     * nothing between there and the `ADC #4` below touches the carry. So the far branch leaves it
     * SET and the cloud ages by FIVE a frame; the near branch runs `SEC / ROL A` on a value whose
     * bit 7 must be clear -- z_hi under 32 shifted twice cannot reach 128 -- and so leaves it
     * CLEAR and the cloud ages by four.
     */
    _math.t = _work[SHIP_Z_OFFSET];
    std::uint8_t scaled = _work[SHIP_Z_OFFSET + 1];
    bool carry = scaled >= 32u;

    if (carry)
    {
      scaled = 0xFEu;
    }
    else
    {
      for (int pass = 0; pass < 2; ++pass)
      {
        const ShiftResult low = RotateLeftValue(_math.t, false); // 6502: ASL T
        _math.t = low.value;
        scaled = RotateLeftValue(scaled, low.carry).value; // 6502: ROL A
      }

      // 6502: SEC / ROL A -- times eight overall, with a 1 forced into bit 0 so that a ship close
      // enough to divide to nothing still has a visible cloud.
      const ShiftResult forced = RotateLeftValue(scaled, true);
      scaled = forced.value;
      carry = forced.carry;
    }

    _math.q = scaled; // 6502: STA Q -- the distance the cloud size is divided by

    const std::uint8_t frump = _heap.Read(static_cast<std::uint16_t>(address + 1u));
    const AddResult grown = AddWithCarry(frump, 4u, carry);

    if (grown.carry)
    {
      // 6502: EX2 -- the counter has run off the end, so the explosion is over. Bits 5 and 7 say
      // "exploding" and "killed", and `MVEIT` is what acts on the pair.
      _work[SHIP_STATE_OFFSET] |= static_cast<std::uint8_t>(SHIP_STATE_EXPLODING | SHIP_STATE_KILLED);
      return;
    }

    _heap.Write(static_cast<std::uint16_t>(address + 1u), grown.value);

    /*
     * 6502: JSR DVID4 -- (P R) = 256 * counter / distance, then times eight, capped at 254.
     *
     * The divide's own exit carry is dropped: `LDA P / CMP #&1C` overwrites it before anything can
     * branch on it. `ASL R / ROL A` three times shifts the sixteen-bit answer up rather than the
     * byte, which is why R is a workspace byte here and not a discarded remainder.
     */
    static_cast<void>(DivideAndScale(_math, grown.value));

    std::uint8_t size = _math.p;
    if (size >= 0x1Cu)
    {
      size = 0xFEu;
    }
    else
    {
      for (int pass = 0; pass < 3; ++pass)
      {
        const ShiftResult low = RotateLeftValue(_math.r, false); // 6502: ASL R
        _math.r = low.value;
        size = RotateLeftValue(size, low.carry).value; // 6502: ROL A
      }
    }

    _heap.Write(address, size); // 6502: DEY / STA (XX19),Y -- byte 0 is this frame's cloud size

    // 6502: AND #%10111111 -- not drawn yet. The following `AND #%00001000` reads what that left,
    // so a ship with nothing on the screen returns here with the flag already cleared.
    _work[SHIP_STATE_OFFSET] = static_cast<std::uint8_t>(_work[SHIP_STATE_OFFSET] & 0xBFu);
    if ((_work[SHIP_STATE_OFFSET] & SHIP_STATE_DRAWN) == 0u)
    {
      return; // 6502: BEQ TT48, which is an RTS
    }

    /*
     * 6502: EXL1 -- copy the visible vertices from `XX3` onto the line heap, downwards.
     *
     * Byte 2 of the heap is `4 * n + 6` for n vertices, so the loop runs from there down to byte 7
     * and the first six bytes -- size, counter, count and three seed bytes -- are left alone. The
     * vertices have to be COPIED rather than read from `XX3` next frame, because `LL9` will have
     * filled `XX3` with a different ship by then.
     *
     * `XX3-7,Y` can address the byte below `XX3` when byte 2 is 6 or less, which no blueprint
     * produces -- the smallest explosion count in the thirty-three is ten. The port reads zero
     * there rather than inventing a neighbour it does not model.
     */
    std::uint8_t index = _heap.Read(static_cast<std::uint16_t>(address + 2u));
    do
    {
      const std::size_t at = static_cast<std::size_t>(index) - 7u;
      _heap.Write(static_cast<std::uint16_t>(address + index), (at < _geometry.xx3.size()) ? _geometry.xx3[at] : std::uint8_t{0});
      --index;
    } while (index != 6u);

    _work[SHIP_STATE_OFFSET] |= SHIP_STATE_CLOUD_DRAWN; // 6502: ORA #%01000000 -- there is a cloud now

    /*
     * 6502: LDY frump / CPY #18 -- the counter BEFORE it grew, so this is true on the explosion's
     * first frame and never again. `PTCLS2S` is a `JMP PTCLS2` that exists only so the branch
     * reaches; the C64 is the only version with either.
     */
    if (frump == EXPLOSION_CLOUD_START)
    {
      DrawParticles(_canvas, _draw, _math, _rng, _work, _heap, _bubble, &_effects);
      return;
    }

    DrawParticles(_canvas, _draw, _math, _rng, _work, _heap, _bubble, nullptr);
  }

} // namespace Elite
