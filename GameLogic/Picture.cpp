#include "pch.h"

#include "Picture.h"

#include "VideoState.h"

#include <vector>

/*
 * The 640x400 surface (Design/Resolution.md, slice RS-0).
 *
 * Everything here is integer and deterministic, as the rest of `GameLogic` is: this file draws a
 * picture and takes no decision the game can see. What it MAY NOT do is as important as what it
 * does -- no random number, no canvas write, no game byte moved -- because the whole claim that the
 * screen is a second rendering rather than a second game rests on it (Resolution.md rule T1).
 */
namespace Elite
{

  namespace
  {
    /// The 4x4 corner of a canvas cell one screen cell shows. A canvas cell is eight pixels square
    /// and doubles into a 2x2 block of screen cells, so each of the four takes a quarter of it.
    constexpr int QUARTER = 4;

    /// The canvas cell that covers a screen cell, and which quarter of it this one is.
    struct Quarter
    {
      int cellColumn = 0; ///< the canvas cell's column
      int cellRow = 0;    ///< and its row
      int acrossIn = 0;   ///< 0 or 4: where in that cell this quarter starts
      int downIn = 0;
    };

    [[nodiscard]] constexpr Quarter QuarterOf(int _column, int _row) noexcept
    {
      return Quarter{_column / 2, _row / 2, (_column & 1) * QUARTER, (_row & 1) * QUARTER};
    }
  } // namespace

  void Picture::Clear() noexcept
  {
    m_bitmap.fill(0);
    m_cells.fill(CellPalette{});
    m_dashboard.fill(0);
    ++m_generation; // a blank picture is a different picture -- see `Generation`
    m_stale = false; // a clear IS the start of a frame, so it settles the debt rather than taking one
  }

  void Picture::Resolve(std::span<std::uint8_t> _out, const Canvas& _canvas, const Picture& _backdrop) const noexcept
  {
    if (_out.size() < static_cast<std::size_t>(WIDTH) * HEIGHT)
    {
      return;
    }

    /*
     * `DFLAG`, through `Canvas::DashboardShown` -- the same flag that decides the canvas's own
     * raster split decides which region the lower rows belong to here.
     *
     * A DOCKED SCREEN HAS NO DASHBOARD REGION AT ALL, which is why this is a flag and not a row
     * comparison: with the dashboard off, all fifty character rows are text over the bitmap plane
     * and the space view's region governs every one of them. That is `Canvas::ResolveCell`'s `lower`
     * test, read from the same byte.
     */
    const bool dashboardShown = _canvas.DashboardShown();

    for (int row = 0; row < CELL_ROWS; ++row)
    {
      const bool dashboardRow = dashboardShown && (row >= DASHBOARD_CELL_ROW);

      for (int column = 0; column < CELL_COLUMNS; ++column)
      {
        std::uint8_t* out = _out.data() + static_cast<std::size_t>(row) * 8 * WIDTH + static_cast<std::size_t>(column) * 8;

        /*
         * TWO KINDS OF CELL AND NOT THREE SINCE RS-6. The third was the canvas doubled, drawn for
         * whatever region had no twins yet, and there is no such region any more: every pixel on
         * this surface is drawn at 640x400 by a twin of the routine that drew the canvas's
         * (Resolution.md section 10, Risk R27). What the canvas is still read for is the raster
         * state below and inside `ResolveBitmapCell` -- the dashboard flag, the energy bomb's mode
         * and background, and the colour RAM a multicolour pixel's %11 comes from.
         */
        if (dashboardRow)
        {
          ResolveDashboardCell(out, _backdrop, column, row);
        }
        else
        {
          ResolveBitmapCell(out, _canvas, _backdrop, column, row);
        }
      }
    }
  }

  std::uint64_t Picture::ResolveSignature(const Canvas& _canvas, const Picture& _backdrop, const VideoState* _video) const noexcept
  {
    /*
     * FNV-1a, the same one `Hash` uses -- over the INPUTS rather than the pixels (see the header).
     *
     * A collision here is a frame the player does not see, and 64 bits over the eleven hundred
     * bytes below makes that a number too small to plan around. The nearer hazard by far is the
     * list being short of a read, which is what `PictureTests` is for.
     */
    constexpr std::uint64_t OFFSET_BASIS = 0xCBF29CE484222325ull;
    constexpr std::uint64_t PRIME = 0x100000001B3ull;

    std::uint64_t hash = OFFSET_BASIS;
    const auto foldByte = [&hash](std::uint8_t _byte) noexcept { hash = (hash ^ static_cast<std::uint64_t>(_byte)) * PRIME; };
    const auto foldWord = [&foldByte](std::uint32_t _value) noexcept
    {
      for (int shift = 0; shift < 32; shift += 8)
      {
        foldByte(static_cast<std::uint8_t>((_value >> shift) & 0xFFu));
      }
    };

    foldWord(m_generation);
    foldWord(_backdrop.m_generation); // both surfaces are resolved, so both decide the answer

    // The colour RAM, which `ResolveBitmapCell` reads for a multicolour cell's %11 pair.
    for (int cell = 0; cell < Canvas::CELL_COLUMNS * Canvas::CELL_ROWS; ++cell)
    {
      foldByte(_canvas.CellColour(cell));
    }

    // The raster state, which this surface does not hold.
    foldByte(_canvas.DashboardShown() ? 1u : 0u);
    foldByte(_canvas.SpaceViewMulticolour() ? 1u : 0u);
    foldByte(ColourIndex(_canvas.SpaceViewBackground()));
    foldByte(ColourIndex(_canvas.Background()));

    // santana and lotus, and the eight pointers `CompositeSprites` reads out of screen memory.
    for (const std::uint8_t byte : _canvas.SpriteMulticolour())
    {
      foldByte(byte);
    }
    for (const Colour colour : _canvas.ExplosionColour())
    {
      foldByte(ColourIndex(colour));
    }
    for (std::size_t sprite = 0; sprite < SPRITE_COUNT; ++sprite)
    {
      foldByte(_canvas.Read(static_cast<std::uint16_t>(Canvas::SPRITE_POINTERS + sprite)));
    }

    foldByte((_video != nullptr) ? 1u : 0u);
    if (_video != nullptr)
    {
      foldByte(_video->enabled);
      foldByte(_video->expanded);
      for (std::size_t sprite = 0; sprite < SPRITE_COUNT; ++sprite)
      {
        foldWord(_video->x[sprite]);
        foldByte(_video->y[sprite]);
        foldByte(ColourIndex(_video->colour[sprite]));
      }
    }

    return hash;
  }

  void Picture::Resolve(std::span<std::uint8_t> _out, const Canvas& _canvas, const Picture& _backdrop,
                        const VideoState& _video) const noexcept
  {
    Resolve(_out, _canvas, _backdrop);

    // The same eight hardware sprites over the same bitmap, at twice the coordinates and from the
    // same definitions (ADR-005 §1, Resolution.md §3.3 and §5.4). One blit serves both surfaces.
    CompositeSprites(_out, WIDTH, HEIGHT, SPACE_VIEW_HEIGHT, 2, _canvas, _video);
  }

  void Picture::ResolveBitmapCell(std::uint8_t* _out, const Canvas& _canvas, const Picture& _backdrop, int _column, int _row) const noexcept
  {
    /*
     * The two surfaces composited by exclusive-or -- the header says why that is the only operation
     * that leaves today's picture unchanged.
     *
     * The PALETTE is composited on its byte and not on its two colours, which is the same thing:
     * the byte is high in the upper nibble and low in the lower, so exclusive-oring it is
     * exclusive-oring each colour index. A cell written on only one surface therefore reads that
     * surface's, and a cell written on both is nonsense -- which the site table forbids and
     * `ThePictureIsAsRecorded` would catch.
     */
    const std::size_t cell = static_cast<std::size_t>(_row) * CELL_COLUMNS + _column;
    const CellPalette palette = CellPalette::Of(static_cast<std::uint8_t>(_backdrop.m_cells[cell].Byte() ^ m_cells[cell].Byte()));
    const std::uint8_t high = ColourIndex(palette.High());
    const std::uint8_t low = ColourIndex(palette.Low());

    const std::size_t base = static_cast<std::size_t>(_row) * ROW_BYTES + static_cast<std::size_t>(_column) * 8;
    std::array<std::uint8_t, 8> composited{};
    for (std::size_t subRow = 0; subRow < composited.size(); ++subRow)
    {
      composited[subRow] = static_cast<std::uint8_t>(_backdrop.m_bitmap[base + subRow] ^ m_bitmap[base + subRow]);
    }
    const std::uint8_t* bits = composited.data();

    /*
     * `moonflower`'s bit 4, read from the canvas because that is where the game keeps it.
     *
     * The energy bomb reinterprets the SAME BYTES as two-bit pairs instead of one-bit pixels, which
     * is why the bomb scrambles the view rather than tinting it -- and it does that to this surface
     * at the moment it does it to the canvas, from one flag, because there is only one bomb. The
     * fourth colour is the canvas's colour RAM for the cell this one sits in: %11 is a game byte and
     * this surface has no second copy of it (§3.2).
     */
    if (_canvas.SpaceViewMulticolour())
    {
      const Quarter quarter = QuarterOf(_column, _row);
      const std::uint8_t colourRam = _canvas.CellColour(quarter.cellRow * Canvas::CELL_COLUMNS + quarter.cellColumn);
      const std::array<std::uint8_t, 4> colours = {ColourIndex(_canvas.SpaceViewBackground()), high, low,
                                                   static_cast<std::uint8_t>(colourRam & 0x0Fu)};

      for (int subRow = 0; subRow < 8; ++subRow)
      {
        std::uint8_t* line = _out + static_cast<std::size_t>(subRow) * WIDTH;
        for (int pixel = 0; pixel < 4; ++pixel)
        {
          const std::uint8_t colour = colours[(bits[subRow] >> (6 - 2 * pixel)) & 0x03u];
          line[pixel * 2] = colour;
          line[pixel * 2 + 1] = colour;
        }
      }
      return;
    }

    // Standard bitmap mode, which is every ordinary frame: one bit, one pixel, and the cell's two
    // colours. There is no background register in this mode and no colour RAM is read.
    for (int subRow = 0; subRow < 8; ++subRow)
    {
      std::uint8_t* line = _out + static_cast<std::size_t>(subRow) * WIDTH;
      for (int pixel = 0; pixel < 8; ++pixel)
      {
        line[pixel] = (((bits[subRow] >> (7 - pixel)) & 1u) != 0u) ? high : low;
      }
    }
  }

  void Picture::ResolveDashboardCell(std::uint8_t* _out, const Picture& _backdrop, int _column, int _row) const noexcept
  {
    /*
     * The dashboard plane is already colour indices, one per pixel, so a cell of it is the two
     * surfaces exclusive-ored -- which is what the blips and the compass dot were doing to this
     * plane on one surface already (`ExclusiveOrDot`), on the index rather than on a bit.
     */
    const std::size_t base = static_cast<std::size_t>(_row * 8 - SPACE_VIEW_HEIGHT) * WIDTH + static_cast<std::size_t>(_column) * 8;

    for (int subRow = 0; subRow < 8; ++subRow)
    {
      const std::size_t at = base + static_cast<std::size_t>(subRow) * WIDTH;
      const std::uint8_t* source = &m_dashboard[at];
      const std::uint8_t* behind = &_backdrop.m_dashboard[at];
      std::uint8_t* line = _out + static_cast<std::size_t>(subRow) * WIDTH;
      for (int pixel = 0; pixel < 8; ++pixel)
      {
        line[pixel] = static_cast<std::uint8_t>((behind[pixel] ^ source[pixel]) & 0x0Fu);
      }
    }
  }

  std::uint64_t Picture::Hash(const Canvas& _canvas, const Picture& _backdrop) const
  {
    constexpr std::uint64_t OFFSET_BASIS = 14695981039346656037ull;
    constexpr std::uint64_t PRIME = 1099511628211ull;

    /*
     * A hash of what `Resolve` PRODUCES, for `Canvas::Hash`'s reason: what a golden asserts is what
     * a person would see, so a change of representation that produced the same picture should not
     * fail one and a surface that resolves differently should.
     *
     * On the heap and not the stack, which is the one way this differs from `Canvas::Hash`: 256,000
     * bytes is a quarter of a default Windows stack, and a test that overflowed it would report
     * something other than the golden it was checking.
     */
    std::vector<std::uint8_t> resolved(static_cast<std::size_t>(WIDTH) * HEIGHT, std::uint8_t{0});
    Resolve(resolved, _canvas, _backdrop);

    std::uint64_t hash = OFFSET_BASIS;
    for (const std::uint8_t index : resolved)
    {
      hash ^= index;
      hash *= PRIME;
    }
    return hash;
  }

} // namespace Elite
