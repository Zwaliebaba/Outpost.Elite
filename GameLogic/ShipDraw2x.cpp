#include "pch.h"

#include "ShipDraw2x.h"

#include "Lines2x.h"

namespace Elite
{

  void DrawShipLines2x(Picture& _picture, const LineHeap& _heap, HeapOffset _run) noexcept
  {
    // `LL155`'s own walk: byte 0 is the run's length and under four there is not a whole line.
    const std::uint8_t length = _heap.Read(_run);
    if (length < 4u)
    {
      return;
    }

    std::uint8_t at = 1;
    do
    {
      Line line;
      line.x1 = _heap.Read(_run.Byte(at));
      line.y1 = _heap.Read(_run.Byte(static_cast<std::uint16_t>(at + 1u)));
      line.x2 = _heap.Read(_run.Byte(static_cast<std::uint16_t>(at + 2u)));
      line.y2 = _heap.Read(_run.Byte(static_cast<std::uint16_t>(at + 3u)));
      DrawLine2x(_picture, line);

      at = static_cast<std::uint8_t>(at + 4);
    } while (at < length);
  }

} // namespace Elite
