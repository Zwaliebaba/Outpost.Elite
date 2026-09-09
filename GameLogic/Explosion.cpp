#include "pch.h"

#include "Explosion.h"

#include "Lines2x.h"

#include "EliteTypes.h"

#include <array>
#include <cstdint>

/*
 * The explosion cloud (slice 4b-b).
 *
 * DOEXP, PTCLS, PTCLS2 and EXS1. Three hundred and eleven instructions between them, and
 * almost all of the difficulty is in flags: five of the routine's decisions are carried into an
 * `ADC` or an `SBC` that has no `CLC` or `SEC` in front of it, so the arithmetic depends on a
 * comparison made several instructions earlier. Each one is named where it happens.
 */
namespace Elite
{

  namespace
  {
    /*
     * PTCLS and PTCLS2, which are one body -- see the header. The two pointers are null for
     * `PTCLS` and the state for `PTCLS2`, which is what tells the two entries apart.
     */
    void DrawParticles(Canvas& _canvas, MathWorkspace& _math, Rng& _rng, const Ship& _work, LineHeap& _heap, const Bubble& _bubble,
                       VideoState* _video, MemoryMap* _map, Picture* _picture) noexcept
    {
      const HeapOffset address = _work.heap;

      /*
       * Sprx and spry -- where the burst sits relative to the cloud's centre.
       *
       * They are RAM in the original and locals here, because `PTCLS2` writes them at the top and
       * reads them further down the same call; nothing else in the game touches either byte.
       */
      std::uint8_t spriteX = 0;
      std::uint8_t spriteY = 0;

      if (_video != nullptr)
      {
        SetMemoryMap(*_map, MEMORY_MAP_IO); // Map the I/O page in

        // The distance compared against 7 with the accumulator already loaded for the
        // register write, so the two answers are chosen before the branch rather than after it.
        const bool distant = _work.z.hi >= 7u;
        ApplySpriteExpansion(*_video, distant ? 0xFDu : 0xFFu);
        spriteX = distant ? 44u : 32u;
        spriteY = distant ? 40u : 30u;
      }

      /*
       * Byte 0 of the heap -- this frame's cloud size, into `Q`.
       *
       * `Q` stays in the workspace where `T`, `U`, `CNT` and `TGT` became locals, for the same
       * reason the clipper's did (M2-c-2): it is the frame's `Q`, and the altitude check takes
       * whatever the frame last left there as its radicand's low byte (§8, R22). `DOEXP` runs
       * inside `LL9` part 9, so on a frame whose last ship exploded this is that byte.
       */
      _math.lastDivisor = _heap.Read(address);

      /*
       * Byte 1, the cloud counter, turned into a particle count.
       *
       * Past 128 the counter is COMPLEMENTED, which is what makes the cloud grow and then shrink: the count
       * is complemented, so the count walks 1..7 up and 7..1 back down over the explosion's life.
       * Four shifts rather than the cassette version's three -- the C64 draws half as many.
       */
      std::uint8_t counter = _heap.Read(address.Byte(static_cast<std::uint16_t>(1u)));
      if ((counter & 0x80u) != 0u)
      {
        counter ^= 0xFFu;
      }
      // `U`, `TGT` and `CNT` are `PTCLS`'s own since M2-c-3: it fills each before its first read
      // and `DOEXP` is the last thing that happens to a ship in a frame. `Q` is not -- see below.
      const std::uint8_t particles = static_cast<std::uint8_t>((counter >> 4) | 1u); // Four shifts, a bit forced on, into U
      const std::uint8_t lastVertex = _heap.Read(address.Byte(static_cast<std::uint16_t>(2u)));

      // One byte of the generator's state pushed and kept across the whole routine, because
      // everything below deliberately destroys that state and one byte of it has to survive.
      const std::uint8_t stacked = _rng.State()[1];

      std::uint8_t vertex = 6; // The byte before the first vertex on the heap

      do
      {
        /*
         * Four bytes off the heap into `K3`, BACKWARDS, so that
         * `K3+3 = x_lo`, `K3+2 = x_hi`, `K3+1 = y_lo`, `K3+0 = y_hi`.
         *
         * These four are `K3` in the original and therefore also `XX2+0` to `XX2+3`, the first
         * four face flags -- the aliasing `GeometryWorkspace::xx2` already warns about. They are
         * locals here: `DOEXP` is the last thing that happens to a ship in a frame, and every
         * reader of `XX2`, `K3` and `K4` writes them before it reads them.
         */
        std::array<std::uint8_t, 4> point{};
        for (int index = 3; index >= 0; --index)
        {
          ++vertex;
          point[static_cast<std::size_t>(index)] = _heap.Read(address.Byte(static_cast<std::uint16_t>(vertex)));
        }
        const std::uint8_t savedVertex = vertex; // Kept in CNT

        if (_video != nullptr)
        {
          /*
           * The burst sprite, placed at the vertex plus the offset chosen above.
           *
           * Both coordinates are sixteen-bit adds with an explicit `CLC`, and both are rejected on
           * the HIGH byte first: negative or 512 and over in x, anything at all in y. The x test is
           * two branches because a sprite's x really is nine bits wide, and the y test is one
           * because a screen row is not.
           */
          const AddResult lowX = AddWithCarry(point[3], spriteX, false);
          const AddResult highX = AddWithCarry(point[2], 0, lowX.carry);

          if ((highX.value & 0x80u) == 0u && highX.value < 2u)
          {
            const AddResult lowY = AddWithCarry(point[1], spriteY, false);
            const AddResult highY = AddWithCarry(point[0], 0, lowY.carry);

            if (highY.value == 0u && lowY.value < EXPLOSION_SPRITE_BOTTOM)
            {
              ApplyExplosionSprite(*_video, static_cast<std::uint16_t>(lowX.value | (highX.value << 8)), lowY.value);
            }
          }
        }

        /*
         * Bytes 3 to 6 of the heap, EORed with the vertex index, become the four
         * generator seeds. The store's index runs from 3 to 6 and wraps to &0002, which is `RAND`.
         *
         * This is the whole trick of the routine: the cloud is regenerated from these four bytes
         * every frame, so drawing it twice erases it, and EORing with the index is what stops all
         * of a ship's vertices blooming the same cloud.
         */
        std::array<std::uint8_t, 4> seeds{};
        for (std::size_t byte = 0; byte < 4u; ++byte)
        {
          seeds[byte] = static_cast<std::uint8_t>(_heap.Read(address.Byte(static_cast<std::uint16_t>(3u + byte))) ^ savedVertex);
        }
        _rng.SetState(seeds);

        // The body runs U + 1 times, not U, because the count is tested after it.
        std::uint8_t particle = particles;
        for (;;)
        {
          // How far away the particle is, which is what decides whether `PIXEL`
          // draws one mark, two, or a square. `ZZ` and `Y1` are this loop's own (M2-c).
          const std::uint8_t distance = _rng.NextRepeatable().value;

          // EXS1 on the vertex's y, against the cloud size in `Q`.
          const ExplosionOffset offsetY = OffsetByCloud(_rng, point[0], point[1], _math.lastDivisor);

          if (offsetY.high != 0u || offsetY.low >= EXPLOSION_PARTICLE_BOTTOM)
          {
            /*
             * EX11 -- and it is not a bare jump to `EX4`. It runs the generator once MORE before
             * rejoining, so that a particle rejected on its y costs the same two random numbers as
             * one that got as far as its x. Without it the cloud would not repeat and could not be
             * erased.
             */
            static_cast<void>(_rng.NextRepeatable());
          }
          else
          {
            const std::uint8_t y1 = offsetY.low; // Kept in Y1

            const ExplosionOffset offsetX = OffsetByCloud(_rng, point[2], point[3], _math.lastDivisor);

            if (offsetX.high == 0u)
            {
              PlotPixel(_canvas, offsetX.low, y1, distance);
              if (DrawingTwins(_picture))
              {
                // The particle's two offsets are eight-bit screen coordinates the cloud's own
                // generator produced, with nothing under them: `EXS1` adds a byte to a byte. So the
                // wide mark is those numbers doubled, and what the resolution buys the cloud is
                // thinness -- a shower of points rather than of two-pixel smears.
                PlotPixel2x(*_picture, 2 * static_cast<int>(offsetX.low), 2 * static_cast<int>(y1), distance);
              }
            }
          }

          --particle;
          if ((particle & 0x80u) != 0u)
          {
            break;
          }
        }

        vertex = savedVertex; // Back out of CNT
      } while (vertex < lastVertex);

      /*
       * The pushed byte pulled back, then the planet's own byte into `RAND+3`.
       *
       * Three fates for four bytes: `RAND+1` comes back off the stack, `RAND+3` is replaced by the
       * PLANET's z_lo -- byte 6 of slot 0, the same "pretty random" byte the spawner reads -- and
       * `RAND` and `RAND+2` keep whatever the last particle left. The next `DORND` anywhere in the
       * game runs on that mixture, so it is part of the routine's answer and not tidying up.
       */
      std::array<std::uint8_t, 4> state = _rng.State();
      state[1] = stacked;

      if (_map != nullptr)
      {
        SetMemoryMap(*_map, MEMORY_MAP_RAM); // Map the I/O page back out
      }

      state[3] = _bubble.blocks[0].z.lo;
      _rng.SetState(state);
    }
  } // namespace

  ExplosionOffset OffsetByCloud(Rng& _rng, std::uint8_t _high, std::uint8_t _low, std::uint8_t _size) noexcept
  {
    const std::uint8_t vertexHigh = _high; // Kept in S -- the vertex's high byte, for the tail

    // The inlined copy of DORND2 -- the C64 spells the routine out here rather than calling
    // it, which changes the timing and nothing else.
    const RngResult random = _rng.NextRepeatable();
    const ShiftResult doubled = RotateLeftValue(random.value, random.carry);

    /*
     * `FMLTU` parks X in `P`, and this is the one call site in the game where the port can
     * say what X held -- the generator's PREVIOUS byte, moved there two instructions back. It
     * byte. `FMLTU` parks X there to preserve it and every exit reloads it, which leaves `P`
     * holding a register value nothing goes on to read. See `MultiplyByLog` in `Arith.h`.
     */
    (void)random.previous;

    if (doubled.carry)
    {
      // The negative half. The carry is SET here BECAUSE the branch was taken, and
      // `FMLTU` passes an entry carry straight through on its two zero exits, so it matters.
      const LogProduct product = MultiplyByLog(doubled.value, _size, true);
      const std::uint8_t offsetLow = product.value;

      // The offset taken off the vertex, and the borrow going in is whatever `FMLTU` left,
      // not a set carry.
      const SubResult low = SubtractWithCarry(_low, offsetLow, product.carry);
      const SubResult high = SubtractWithCarry(vertexHigh, 0, low.carry);
      return ExplosionOffset{high.value, low.value};
    }

    // FMLTU again for the positive half, and the same borrowed carry the other way round.
    // §6.42 recorded this call as one of the two that read it.
    const LogProduct product = MultiplyByLog(doubled.value, _size, false);
    const AddResult low = AddWithCarry(product.value, _low, product.carry);
    const AddResult high = AddWithCarry(vertexHigh, 0, low.carry);
    return ExplosionOffset{high.value, low.value};
  }

  void DrawExplosionParticles(Canvas& _canvas, MathWorkspace& _math, Rng& _rng, const Ship& _work, LineHeap& _heap,
                              const Bubble& _bubble, Picture* _picture) noexcept
  {
    DrawParticles(_canvas, _math, _rng, _work, _heap, _bubble, nullptr, nullptr, _picture);
  }

  void DrawExplosionParticlesWithSprite(Canvas& _canvas, MathWorkspace& _math, Rng& _rng, const Ship& _work, LineHeap& _heap,
                                        const Bubble& _bubble, VideoState& _video, MemoryMap& _map, Picture* _picture) noexcept
  {
    DrawParticles(_canvas, _math, _rng, _work, _heap, _bubble, &_video, &_map, _picture);
  }

  void DrawExplosionCloud(Canvas& _canvas, MathWorkspace& _math, Rng& _rng, Ship& _work, LineHeap& _heap, const GeometryWorkspace& _geometry,
                          const Bubble& _bubble, VideoState& _video, MemoryMap& _map, Picture* _picture) noexcept
  {
    const HeapOffset address = _work.heap;

    // Bit 6 of byte 31 -- there is a cloud on the screen from last frame, so draw it again
    // to rub it out. Always through `PTCLS`; the burst sprite is placed once and left alone.
    if (Has(_work.state, ShipStateBit::CloudDrawn))
    {
      /*
       * NO TWIN ON THE FRAME: the comment above says what this is -- last frame's cloud drawn a
       * second time to rub it out -- and the frame is cleared each pass (RN-1). On the canvas the
       * second draw is the erase; on a surface blanked every pass it is a ghost of the cloud that
       * has already gone, and the new cloud is drawn over it.
       */
      DrawParticles(_canvas, _math, _rng, _work, _heap, _bubble, nullptr, nullptr, nullptr);
    }

    /*
     * (A T) = z, scaled into one byte -- and the CARRY IT LEAVES IS THE POINT.
     *
     * The comparison against 32 decides whether the ship is far enough away to cap the distance
     * at 254, and nothing between there and the addition of four below touches the carry. So
     * the far branch leaves it SET and the cloud ages by FIVE a frame; the near branch rotates
     * a value whose bit 7 must be clear -- z_hi under 32 shifted twice cannot reach 128 -- and
     * so leaves it CLEAR and the cloud ages by four.
     */
    std::uint8_t scaleLow = _work.z.lo; // `DOEXP`'s own since M2-c-3, the low half of the scale
    std::uint8_t scaled = _work.z.hi;
    bool carry = scaled >= 32u;

    if (carry)
    {
      scaled = 0xFEu;
    }
    else
    {
      for (int pass = 0; pass < 2; ++pass)
      {
        const ShiftResult low = RotateLeftValue(scaleLow, false); // The low byte doubled
        scaleLow = low.value;
        scaled = RotateLeftValue(scaled, low.carry).value; // And the high byte rotated
      }

      // A set carry rotated in -- times eight overall, with a 1 forced into bit 0 so that a ship close
      // enough to divide to nothing still has a visible cloud.
      const ShiftResult forced = RotateLeftValue(scaled, true);
      scaled = forced.value;
      carry = forced.carry;
    }

    _math.lastDivisor = scaled; // Into Q -- the distance the cloud size is divided by

    const std::uint8_t cloudCounter = _heap.Read(address.Byte(static_cast<std::uint16_t>(1u)));
    const AddResult grown = AddWithCarry(cloudCounter, 4u, carry);

    if (grown.carry)
    {
      // The counter has run off the end, so the explosion is over. Bits 5 and 7 say
      // "exploding" and "killed", and `MVEIT` is what acts on the pair.
      _work.state = With(_work.state, ShipStateBit::Exploding, ShipStateBit::Killed);
      return;
    }

    _heap.Write(address.Byte(static_cast<std::uint16_t>(1u)), grown.value);

    /*
     * (P R) = 256 * counter / distance, then times eight, capped at 254.
     *
     * The divide's own exit carry is dropped: the comparison that follows overwrites it before
     * anything can branch on it. The three doublings shift the sixteen-bit answer up rather than
     * byte, which is why R is a workspace byte here and not a discarded remainder.
     */
    const ScaledDivision divided = DivideAndScale(grown.value, _math.lastDivisor);

    std::uint8_t size = divided.whole;
    if (size >= 0x1Cu)
    {
      size = 0xFEu;
    }
    else
    {
      std::uint8_t fraction = divided.fraction;
      for (int pass = 0; pass < 3; ++pass)
      {
        const ShiftResult low = RotateLeftValue(fraction, false); // The fraction doubled
        fraction = low.value;
        size = RotateLeftValue(size, low.carry).value; // And the size rotated
      }
    }

    _heap.Write(address, size); // Back into byte 0 -- this frame's cloud size

    // The drawn bit masked off -- not drawn yet. The test that follows reads what that
    // left, so a ship with nothing on the screen returns here with the flag already cleared.
    _work.state = Without(_work.state, ShipStateBit::CloudDrawn);
    if (!Has(_work.state, ShipStateBit::OnScreen))
    {
      return; // TT48, which is a bare return
    }

    /*
     * Copy the visible vertices from `XX3` onto the line heap, downwards.
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
    std::uint8_t index = _heap.Read(address.Byte(static_cast<std::uint16_t>(2u)));
    do
    {
      const std::size_t at = static_cast<std::size_t>(index) - 7u;
      _heap.Write(address.Byte(static_cast<std::uint16_t>(index)), (at < _geometry.projectedVertices.size()) ? _geometry.projectedVertices[at] : std::uint8_t{0});
      --index;
    } while (index != 6u);

    _work.state = With(_work.state, ShipStateBit::CloudDrawn); // The drawn bit set -- there is a cloud now

    /*
     * The counter compared against 18 BEFORE it grew, so this is true on the
     * explosion's first frame and never again. `PTCLS2S` is a jump that exists only so the branch
     * reaches; the C64 is the only version with either.
     */
    if (cloudCounter == EXPLOSION_CLOUD_START)
    {
      DrawParticles(_canvas, _math, _rng, _work, _heap, _bubble, &_video, &_map, _picture);
      return;
    }

    DrawParticles(_canvas, _math, _rng, _work, _heap, _bubble, nullptr, nullptr, _picture);
  }

} // namespace Elite
