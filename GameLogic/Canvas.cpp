#include "pch.h"

#include "Canvas.h"

#include "LookupTables.h"
#include "VideoState.h"

namespace Elite
{

  namespace
  {
    /*
     * The four colours a multicolour cell can offer, in the order its two bits select them.
     *
     * A helper rather than four lines inside the decode, because `Screen` needs the same tuple for
     * the energy bomb's reinterpretation of its own bits (Design/Resolution.md section 3.2) and the
     * order -- background, high nibble, low nibble, colour RAM -- is the thing to state once.
     */
    std::array<std::uint8_t, 4> MulticolourChoices(std::uint8_t _cellByte, std::uint8_t _colourRam, Colour _background) noexcept
    {
      return {ColourIndex(_background), static_cast<std::uint8_t>(_cellByte >> 4), static_cast<std::uint8_t>(_cellByte & 0x0Fu),
              static_cast<std::uint8_t>(_colourRam & 0x0Fu)};
    }
  } // namespace

  std::array<std::uint8_t, 4> Canvas::DashboardChoices(int _cell) const noexcept
  {
    if (_cell < 0 || _cell >= CELL_COLUMNS * CELL_ROWS)
    {
      return {0, 0, 0, 0};
    }
    return MulticolourChoices(m_screen[DASHBOARD_CELLS + _cell], m_colourCells[_cell], m_background);
  }

  /*
   * One character cell, resolved into eight rows of the output image.
   *
   * The two modes are one function because everything except the innermost loop is shared -- the
   * cell's byte in screen RAM, the walk down its eight sub-rows, where in the output each one goes.
   * Splitting them would duplicate all of that so that the two halves of a split screen could
   * disagree about it, which is the bug this file once had.
   */
  void Canvas::ResolveCell(int _cellColumn, int _cellRow, std::uint8_t* _out, int _stride) const noexcept
  {
    if (_cellColumn < 0 || _cellColumn >= CELL_COLUMNS || _cellRow < 0 || _cellRow >= CELL_ROWS)
    {
      return;
    }

    /*
     * Where `comirq1`'s raster split falls. The interrupt reprograms VIC registers &16 and
     * &18 at the top of the dashboard, so a row below it is multicolour and coloured from the
     * second block of screen RAM -- but only while the dashboard is actually there. On a text
     * view `abraxas` and `caravanserai` are left alone and the whole screen is standard.
     */
    const bool lower = m_dashboardShown && (_cellRow >= DASHBOARD_CELL_ROW);
    const std::uint16_t cellBase = lower ? DASHBOARD_CELLS : SCREEN_CELLS;

    /*
     * moonflower and welcome, the other half of the same interrupt's pair.
     *
     * Above the split the mode is `moonflower`'s bit 4 and the background is `welcome`, and both
     * of them move only while the energy bomb burns -- so for every ordinary frame this is the
     * standard-mode branch it always was. The cell BLOCK does not follow the mode: `zebop` is
     * &81 whatever happens, so the upper half is coloured from the first block either way.
     */
    const bool multicolour = lower || m_spaceViewMulticolour;
    const Colour background = lower ? m_background : m_spaceViewBackground;

    const int cell = _cellRow * CELL_COLUMNS + _cellColumn;
    const std::uint8_t cellByte = m_screen[cellBase + cell];
    const std::uint8_t* bitmap = &m_screen[_cellRow * ROW_BYTES + _cellColumn * 8];

    // The nibbles of the cell's byte in screen RAM. Both modes read them; they differ only
    // in what selects between them.
    const std::uint8_t high = static_cast<std::uint8_t>(cellByte >> 4);
    const std::uint8_t low = static_cast<std::uint8_t>(cellByte & 0x0Fu);
    const std::array<std::uint8_t, 4> colours = MulticolourChoices(cellByte, m_colourCells[cell], background);

    for (int subRow = 0; subRow < 8; ++subRow)
    {
      const std::uint8_t bits = bitmap[subRow];
      std::uint8_t* row = _out + static_cast<std::size_t>(subRow) * _stride;

      if (multicolour)
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

  void Canvas::Clear() noexcept
  {
    m_screen.fill(0);
    m_colourCells.fill(0);
    m_background = Colour::Black;
    m_spaceViewBackground = Colour::Black;
    m_spaceViewMulticolour = false;
  }

  void Canvas::Resolve(std::span<std::uint8_t> _out) const noexcept
  {
    if (_out.size() < static_cast<std::size_t>(WIDTH) * HEIGHT)
    {
      return;
    }

    for (int cellRow = 0; cellRow < CELL_ROWS; ++cellRow)
    {
      for (int cellColumn = 0; cellColumn < CELL_COLUMNS; ++cellColumn)
      {
        ResolveCell(cellColumn, cellRow, _out.data() + static_cast<std::size_t>(cellRow) * 8 * WIDTH + cellColumn * 8, WIDTH);
      }
    }
  }

  namespace
  {
    /// The colour a HI-RES sprite pixel takes, or -1 for transparent: one bit per pixel, set is the
    /// sprite's own colour and clear is the bitmap showing through.
    [[nodiscard]] int HiresPixel(const std::uint8_t* _row, int _column, Colour _colour) noexcept
    {
      const std::uint8_t byte = _row[_column >> 3];
      const std::uint8_t bit = static_cast<std::uint8_t>(0x80u >> (_column & 7));
      return ((byte & bit) != 0u) ? static_cast<int>(ColourIndex(_colour)) : -1;
    }

    /*
     * The colour a MULTICOLOUR sprite pixel takes, or -1 for transparent.
     *
     * Two bits per pixel and twelve pixels across, each two dots wide. %00 is transparent, %10 is
     * the sprite's own colour, and %01 and %11 come from the two registers every multicolour sprite
     * SHARES -- which is why a Trumble cannot be recoloured on its own, and why the original never
     * tries to.
     */
    [[nodiscard]] int MulticolourPixel(const std::uint8_t* _row, int _pair, Colour _colour) noexcept
    {
      const std::uint8_t byte = _row[_pair >> 2];
      const int shift = 6 - 2 * (_pair & 3);
      switch ((byte >> shift) & 3u)
      {
      case 1u:
        return static_cast<int>(ColourIndex(SPRITE_MULTICOLOUR_1));
      case 2u:
        return static_cast<int>(ColourIndex(_colour));
      case 3u:
        return static_cast<int>(ColourIndex(SPRITE_MULTICOLOUR_2));
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
    /// What the VIC-II holds about one sprite while it is being drawn: its own colour register, and
    /// the two the raster split rewrites, each as the pair `COMIRQ1` programs them in.
    struct SpriteRegisters
    {
      int sprite = 0;                               ///< which of the eight, because &1C is indexed by it
      Colour colour = Colour::Black;                ///< VIC+&27 + N -- this sprite's own colour
      std::span<const std::uint8_t, 2> multicolour; ///< [0] the space view's, [1] the dashboard's
      std::span<const Colour, 2> explosion;         ///< VIC+&28, and sprite 1 is the only reader
    };

    /// `_width`, `_height` and `_splitRow` are the OUTPUT's, because both surfaces composite the
    /// same eight sprites and only their geometry differs (Design/Resolution.md section 3.3).
    void BlitSprite(std::uint8_t* _out, const std::uint8_t* _definition, const SpriteRegisters& _registers, int _left, int _top, int _scale,
                    int _width, int _height, int _splitRow) noexcept
    {
      /*
       * ROW BY ROW, AND THE MODE IS DECIDED INSIDE THE LOOP.
       *
       * `COMIRQ1` rewrites VIC+&1C and VIC+&28 at the raster split, so a sprite that straddles it
       * is multicolour on one side and single-colour on the other -- which is not an edge case but
       * the mechanism that keeps explosions out of the dashboard (§6.155). The VIC-II decides this
       * as it scans, so the port decides it per SCREEN row: an expanded sprite's two output rows
       * come from one sprite row and can land either side.
       *
       * A multicolour sprite is twelve pixels of two dots each; a hi-res one is twenty-four of one.
       * Both are 24 dots wide before expansion, which is why one loop serves either way.
       */
      for (int row = 0; row < SPRITE_ROWS; ++row)
      {
        const std::uint8_t* bytes = _definition + static_cast<std::size_t>(row) * SPRITE_ROW_BYTES;

        for (int down = 0; down < _scale; ++down)
        {
          const int y = _top + (row * _scale) + down;
          if (y < 0 || y >= _height)
          {
            continue;
          }

          const std::size_t half = (y >= _splitRow) ? 1u : 0u;
          const bool multicolour = ((_registers.multicolour[half] >> _registers.sprite) & 1u) != 0u;
          const Colour colour = (_registers.sprite == EXPLOSION_SPRITE) ? _registers.explosion[half] : _registers.colour;

          const int steps = multicolour ? (SPRITE_WIDTH / 2) : SPRITE_WIDTH;
          const int dots = multicolour ? 2 : 1;

          for (int step = 0; step < steps; ++step)
          {
            const int index = multicolour ? MulticolourPixel(bytes, step, colour) : HiresPixel(bytes, step, colour);
            if (index < 0)
            {
              continue; // %00, or a clear bit: the bitmap shows through
            }

            std::uint8_t* line = _out + static_cast<std::size_t>(y) * _width;
            for (int wide = 0; wide < dots * _scale; ++wide)
            {
              const int x = _left + (step * dots * _scale) + wide;
              if (x >= 0 && x < _width)
              {
                line[x] = static_cast<std::uint8_t>(index);
              }
            }
          }
        }
      }
    }
  } // namespace

  void CompositeSprites(std::span<std::uint8_t> _out, int _width, int _height, int _splitRow, int _scale, const Canvas& _canvas,
                        const VideoState& _video) noexcept
  {
    if (_out.size() < static_cast<std::size_t>(_width) * _height)
    {
      return;
    }

    // The VIC-II draws sprite 7 first and sprite 0 last, so a LOWER-numbered sprite is in
    // front. Walking down means the laser sights end up over a Trumble, which is the hardware's
    // order and not a preference.
    for (int sprite = static_cast<int>(SPRITE_COUNT) - 1; sprite >= 0; --sprite)
    {
      if ((_video.enabled & (1u << sprite)) == 0u)
      {
        continue; // VIC+&15 -- switched off
      }

      const std::uint8_t pointer = _canvas.Read(static_cast<std::uint16_t>(Canvas::SPRITE_POINTERS + sprite));
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

      /*
       * The VIC-II's own expand flag TIMES the output's scale, which is what makes one blit serve
       * both surfaces: on the canvas `_scale` is one and this is the flag alone, and on the 640x400
       * screen an ordinary sprite is two output pixels to a dot and an expanded one is four.
       */
      const int expanded = ((_video.expanded & (1u << sprite)) != 0u) ? 2 : 1;

      /*
       * VIC+&1C, and the mode is NOT read off the definition.
       *
       * That is what the port used to do, on a claim that the register is never written; `COMIRQ1`
       * writes it twice a frame and the explosion sprite is the one it moves (§6.155). The
       * registers go in as the split leaves them and `BlitSprite` picks per screen row.
       */
      const SpriteRegisters registers{sprite, _video.colour[sprite], _canvas.SpriteMulticolour(), _canvas.ExplosionColour()};

      BlitSprite(_out.data(), SPRITE_DEFINITIONS.data() + static_cast<std::size_t>(definition) * SPRITE_BYTES, registers,
                 (static_cast<int>(_video.x[sprite]) - SPRITE_ORIGIN_X) * _scale,
                 (static_cast<int>(_video.y[sprite]) - SPRITE_ORIGIN_Y) * _scale, expanded * _scale, _width, _height, _splitRow);
    }
  }

  void Canvas::Resolve(std::span<std::uint8_t> _out, const VideoState& _video) const noexcept
  {
    Resolve(_out);
    CompositeSprites(_out, WIDTH, HEIGHT, SPACE_VIEW_HEIGHT, 1, *this, _video);
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
