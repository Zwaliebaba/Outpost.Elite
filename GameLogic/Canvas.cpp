#include "pch.h"

#include "Canvas.h"

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
