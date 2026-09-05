#include "pch.h"

#include "Canvas.h"

#include "LookupTables.h"
#include "VideoState.h"

namespace Elite
{

  namespace
  {
    /*
     * One character cell, resolved into eight rows of the output image.
     *
     * The two modes are one function because everything except the innermost loop is shared -- the
     * cell's byte in screen RAM, the walk down its eight sub-rows, where in the output each one goes.
     * Splitting them would duplicate all of that so that the two halves of a split screen could
     * disagree about it, which is the bug this file just had.
     */
    void ResolveCell(std::uint8_t* _out, int _outStride, const std::uint8_t* _bitmap, std::uint8_t _cellByte, std::uint8_t _colourRam,
                     std::uint8_t _background, bool _multicolour) noexcept
    {
      // 6502: the nibbles of the cell's byte in screen RAM. Both modes read them; they differ only
      // in what selects between them.
      const std::uint8_t high = static_cast<std::uint8_t>(_cellByte >> 4);
      const std::uint8_t low = static_cast<std::uint8_t>(_cellByte & 0x0Fu);

      // The four colours a multicolour cell can offer, in the order the two bits select them.
      const std::uint8_t colours[4] = {_background, high, low, static_cast<std::uint8_t>(_colourRam & 0x0Fu)};

      for (int subRow = 0; subRow < 8; ++subRow)
      {
        const std::uint8_t bits = _bitmap[subRow];
        std::uint8_t* row = _out + static_cast<std::size_t>(subRow) * _outStride;

        if (_multicolour)
        {
          for (int pixel = 0; pixel < 4; ++pixel)
          {
            const std::uint8_t colour = colours[(bits >> (6 - 2 * pixel)) & 0x03u];

            // A multicolour pixel is two screen columns wide. This is the only doubling in the port,
            // and it is here rather than in the shader so that the canvas the golden tests hash is
            // the image a person would see.
            row[pixel * 2] = colour;
            row[pixel * 2 + 1] = colour;
          }
          continue;
        }

        // Standard bitmap mode: one bit, one pixel, one column. A set bit takes the cell's high
        // nibble and a clear one takes its low nibble -- there is no background register in this
        // mode and colour RAM is not read at all.
        for (int pixel = 0; pixel < 8; ++pixel)
        {
          row[pixel] = (((bits >> (7 - pixel)) & 1u) != 0u) ? high : low;
        }
      }
    }
  } // namespace

  void Canvas::Clear() noexcept
  {
    m_screen.fill(0);
    m_colourCells.fill(0);
    m_background = 0;
  }

  void Canvas::Resolve(std::span<std::uint8_t> _out) const noexcept
  {
    if (_out.size() < static_cast<std::size_t>(WIDTH) * HEIGHT)
    {
      return;
    }

    for (int cellRow = 0; cellRow < CELL_ROWS; ++cellRow)
    {
      /*
       * 6502: where `comirq1`'s raster split falls. The interrupt reprograms VIC registers &16 and
       * &18 at the top of the dashboard, so a row below it is multicolour and coloured from the
       * second block of screen RAM -- but only while the dashboard is actually there. On a text
       * view `abraxas` and `caravanserai` are left alone and the whole screen is standard.
       */
      const bool lower = m_dashboardShown && (cellRow >= DASHBOARD_CELL_ROW);
      const std::uint16_t cellBase = lower ? DASHBOARD_CELLS : SCREEN_CELLS;

      for (int cellColumn = 0; cellColumn < CELL_COLUMNS; ++cellColumn)
      {
        const int cell = cellRow * CELL_COLUMNS + cellColumn;
        ResolveCell(_out.data() + static_cast<std::size_t>(cellRow) * 8 * WIDTH + cellColumn * 8, WIDTH,
                    &m_screen[cellRow * ROW_BYTES + cellColumn * 8], m_screen[cellBase + cell], m_colourCells[cell], m_background, lower);
      }
    }
  }

  namespace
  {
    /*
     * 6502: the last eight bytes of screen RAM -- sprite N's pointer lives at base + &3F8 + N.
     *
     * WHICH BLOCK IS READ CANNOT MATTER, and that is a property of the game rather than a shrug.
     * There are two blocks of screen RAM and the VIC-II reads whichever the raster split has
     * selected, so the pointers exist twice -- and every writer keeps them in step by writing both:
     * the loader stores each pointer to &63Fx AND &67Fx, and `SIGHT` writes `SIGHT_SPRITE_CELL`
     * and `SIGHT_SPRITE_CELL_2` on consecutive lines for the same reason. Reading the first block
     * is therefore reading both. If they ever disagree that is a finding about a writer, not a
     * decision to be taken here.
     */
    constexpr std::uint16_t SPRITE_POINTERS = Canvas::SCREEN_CELLS + 0x3F8u;

    /// The colour a HI-RES sprite pixel takes, or -1 for transparent: one bit per pixel, set is the
    /// sprite's own colour and clear is the bitmap showing through.
    [[nodiscard]] int HiresPixel(const std::uint8_t* _row, int _column, std::uint8_t _colour) noexcept
    {
      const std::uint8_t byte = _row[_column >> 3];
      const std::uint8_t bit = static_cast<std::uint8_t>(0x80u >> (_column & 7));
      return ((byte & bit) != 0u) ? static_cast<int>(_colour) : -1;
    }

    /*
     * The colour a MULTICOLOUR sprite pixel takes, or -1 for transparent.
     *
     * Two bits per pixel and twelve pixels across, each two dots wide. %00 is transparent, %10 is
     * the sprite's own colour, and %01 and %11 come from the two registers every multicolour sprite
     * SHARES -- which is why a Trumble cannot be recoloured on its own, and why the original never
     * tries to.
     */
    [[nodiscard]] int MulticolourPixel(const std::uint8_t* _row, int _pair, std::uint8_t _colour) noexcept
    {
      const std::uint8_t byte = _row[_pair >> 2];
      const int shift = 6 - 2 * (_pair & 3);
      switch ((byte >> shift) & 3u)
      {
      case 1u:
        return SPRITE_MULTICOLOUR_1;
      case 2u:
        return static_cast<int>(_colour);
      case 3u:
        return SPRITE_MULTICOLOUR_2;
      default:
        return -1;
      }
    }

    /*
     * One sprite, over the resolved image.
     *
     * The VIC-II measures a sprite from its own origin rather than from the visible screen, so 24
     * and 50 come off the coordinates: a sprite at (24, 50) sits in the top-left corner. Expansion
     * doubles each pixel and moves nothing -- the top-left corner stays put and the sprite grows
     * down and right, which is what makes `PTCLS2`'s close bursts bloom rather than jump.
     *
     * SPRITES DRAW OVER THE BITMAP, because VIC+&1B is zero and nothing in this build ever writes
     * it: the loader leaves it at its power-on value and the game has no instruction that touches
     * it. So there is no priority to model here, only a rule to state.
     */
    void BlitSprite(std::uint8_t* _out, const std::uint8_t* _definition, bool _multicolour, int _left, int _top, int _scale,
                    std::uint8_t _colour) noexcept
    {
      // A multicolour sprite is twelve pixels of two dots each; a hi-res one is twenty-four of one.
      // Both are 24 dots wide before expansion, which is why one loop serves.
      const int steps = _multicolour ? (SPRITE_WIDTH / 2) : SPRITE_WIDTH;
      const int dots = _multicolour ? 2 : 1;

      for (int row = 0; row < SPRITE_ROWS; ++row)
      {
        const std::uint8_t* bytes = _definition + static_cast<std::size_t>(row) * SPRITE_ROW_BYTES;

        for (int step = 0; step < steps; ++step)
        {
          const int index = _multicolour ? MulticolourPixel(bytes, step, _colour) : HiresPixel(bytes, step, _colour);
          if (index < 0)
          {
            continue; // %00, or a clear bit: the bitmap shows through
          }

          for (int down = 0; down < _scale; ++down)
          {
            const int y = _top + (row * _scale) + down;
            if (y < 0 || y >= Canvas::HEIGHT)
            {
              continue;
            }

            std::uint8_t* line = _out + static_cast<std::size_t>(y) * Canvas::WIDTH;
            for (int wide = 0; wide < dots * _scale; ++wide)
            {
              const int x = _left + (step * dots * _scale) + wide;
              if (x >= 0 && x < Canvas::WIDTH)
              {
                line[x] = static_cast<std::uint8_t>(index);
              }
            }
          }
        }
      }
    }
  } // namespace

  void Canvas::Resolve(std::span<std::uint8_t> _out, const VideoState& _video) const noexcept
  {
    Resolve(_out);

    if (_out.size() < static_cast<std::size_t>(WIDTH) * HEIGHT)
    {
      return;
    }

    // 6502: the VIC-II draws sprite 7 first and sprite 0 last, so a LOWER-numbered sprite is in
    // front. Walking down means the laser sights end up over a Trumble, which is the hardware's
    // order and not a preference.
    for (int sprite = static_cast<int>(SPRITE_COUNT) - 1; sprite >= 0; --sprite)
    {
      if ((_video.enabled & (1u << sprite)) == 0u)
      {
        continue; // 6502: VIC+&15 -- switched off
      }

      const std::uint8_t pointer = Read(static_cast<std::uint16_t>(SPRITE_POINTERS + sprite));
      const int definition = static_cast<int>(pointer) - static_cast<int>(SPRITE_POINTER_ORIGIN);
      if (definition < 0 || definition >= static_cast<int>(SPRITE_DEFINITION_COUNT))
      {
        /*
         * A pointer outside `spritp`, which this build cannot form: the loader sets all eight and
         * `SIGHT` only ever writes SPOFF% + 0 to 3. It is skipped rather than clamped, because
         * clamping would invent a picture -- on the hardware such a pointer shows whatever 64 bytes
         * of memory it lands on, and the honest answer to "what would that look like" is that the
         * port does not model memory outside the canvas at all.
         */
        continue;
      }

      const bool multicolour = static_cast<std::size_t>(definition) >= FIRST_MULTICOLOUR_DEFINITION;
      const int scale = ((_video.expanded & (1u << sprite)) != 0u) ? 2 : 1;

      BlitSprite(_out.data(), SPRITE_DEFINITIONS.data() + static_cast<std::size_t>(definition) * SPRITE_BYTES, multicolour,
                 static_cast<int>(_video.x[sprite]) - SPRITE_ORIGIN_X, static_cast<int>(_video.y[sprite]) - SPRITE_ORIGIN_Y, scale,
                 _video.colour[sprite]);
    }
  }

  std::uint64_t Canvas::Hash() const noexcept
  {
    constexpr std::uint64_t OFFSET_BASIS = 14695981039346656037ull;
    constexpr std::uint64_t PRIME = 1099511628211ull;

    /*
     * The hash is of what `Resolve` PRODUCES, and the cheapest way to keep that true is to resolve
     * and hash rather than to walk the planes a second time in parallel. The old version did walk
     * them twice and the two walks had to be kept in step by hand; they were, but only because
     * nothing had ever changed the decode -- and the first thing that did would have moved the
     * picture without moving the hash, which is exactly the drift a golden exists to catch.
     */
    std::array<std::uint8_t, static_cast<std::size_t>(WIDTH) * HEIGHT> resolved{};
    Resolve(resolved);

    std::uint64_t hash = OFFSET_BASIS;
    for (const std::uint8_t index : resolved)
    {
      hash ^= index;
      hash *= PRIME;
    }
    return hash;
  }

} // namespace Elite
