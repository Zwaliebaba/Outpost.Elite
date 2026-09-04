#include "pch.h"

#include "Canvas.h"

namespace Elite
{

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

  const std::uint16_t cellBase = m_textView ? TEXT_CELLS : SPACE_CELLS;

  for (int cellRow = 0; cellRow < CELL_ROWS; ++cellRow)
  {
    for (int cellColumn = 0; cellColumn < CELL_COLUMNS; ++cellColumn)
    {
      const int cell = cellRow * CELL_COLUMNS + cellColumn;
      const std::uint8_t cellByte = m_screen[cellBase + cell];

      // The three colours a cell can offer besides the background, in the order the two bits
      // select them.
      const std::uint8_t colours[4] = { m_background,
                                        static_cast<std::uint8_t>(cellByte >> 4),
                                        static_cast<std::uint8_t>(cellByte & 0x0Fu),
                                        static_cast<std::uint8_t>(m_colourCells[cell] & 0x0Fu) };

      for (int subRow = 0; subRow < 8; ++subRow)
      {
        const std::uint8_t bits = m_screen[cellRow * ROW_BYTES + cellColumn * 8 + subRow];
        const int y = cellRow * 8 + subRow;
        std::uint8_t* row = _out.data() + static_cast<std::size_t>(y) * WIDTH + cellColumn * 8;

        for (int pixel = 0; pixel < 4; ++pixel)
        {
          const std::uint8_t code = static_cast<std::uint8_t>((bits >> (6 - 2 * pixel)) & 0x03u);
          const std::uint8_t colour = colours[code];

          // A multicolour pixel is two screen columns wide. This is the only doubling in the
          // port, and it is here rather than in the shader so that the canvas the golden tests
          // hash is the image a person would see.
          row[pixel * 2] = colour;
          row[pixel * 2 + 1] = colour;
        }
      }
    }
  }
}

std::uint64_t Canvas::Hash() const noexcept
{
  constexpr std::uint64_t OFFSET_BASIS = 14695981039346656037ull;
  constexpr std::uint64_t PRIME = 1099511628211ull;

  const std::uint16_t cellBase = m_textView ? TEXT_CELLS : SPACE_CELLS;
  std::uint64_t hash = OFFSET_BASIS;

  const auto mix = [&hash](std::uint8_t _byte) noexcept {
    hash ^= _byte;
    hash *= PRIME;
  };

  // The same walk Resolve makes, so the hash is of the picture rather than of the planes.
  for (int cellRow = 0; cellRow < CELL_ROWS; ++cellRow)
  {
    for (int cellColumn = 0; cellColumn < CELL_COLUMNS; ++cellColumn)
    {
      const int cell = cellRow * CELL_COLUMNS + cellColumn;
      const std::uint8_t cellByte = m_screen[cellBase + cell];
      const std::uint8_t colours[4] = { m_background,
                                        static_cast<std::uint8_t>(cellByte >> 4),
                                        static_cast<std::uint8_t>(cellByte & 0x0Fu),
                                        static_cast<std::uint8_t>(m_colourCells[cell] & 0x0Fu) };

      for (int subRow = 0; subRow < 8; ++subRow)
      {
        const std::uint8_t bits = m_screen[cellRow * ROW_BYTES + cellColumn * 8 + subRow];
        for (int pixel = 0; pixel < 4; ++pixel)
        {
          mix(colours[(bits >> (6 - 2 * pixel)) & 0x03u]);
        }
      }
    }
  }

  return hash;
}

} // namespace Elite
