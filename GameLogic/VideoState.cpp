#include "pch.h"

#include "VideoState.h"

namespace Elite
{

  void ApplySightColour(VideoState& _video, Colour _colour) noexcept
  {
    _video.colour[0] = _colour; // Sprite 0's colour register
  }

  void ApplySpritesEnabled(VideoState& _video, std::uint8_t _mask) noexcept
  {
    _video.enabled = _mask; // The sprite enable register
  }

  void ApplyMaskSprites(VideoState& _video, std::uint8_t _mask) noexcept
  {
    // The sprite enable register read, masked to its low two bits and written back -- and
    // the READ is the point. The flight loop's part 15 does not know how many Trumble sprites
    // are showing, so it cannot compute the new byte; it masks whatever is there.
    _video.enabled = static_cast<std::uint8_t>(_video.enabled & _mask);
  }

  void ApplySpriteExpansion(VideoState& _video, std::uint8_t _mask) noexcept
  {
    _video.expanded = _mask; // Both sprite expand registers, vertical and horizontal
  }

  void ApplyExplosionSprite(VideoState& _video, std::uint16_t _x, std::uint8_t _y) noexcept
  {
    /*
     * The five writes that place sprite 1 and switch it on.
     *
     * The nine-bit x arrives whole. On the hardware the low eight go to one register and the
     * ninth is bit 1 of another, set by masking that bit out and OR-ing in a two-byte table whose
     * only job is to shift a 0 or 1 left one place. Keeping it whole here is not a simplification:
     * the split is the register layout's, and the one place that has to imitate a register is the
     * presenter.
     */
    _video.x[1] = _x;
    _video.y[1] = _y;
    _video.enabled = static_cast<std::uint8_t>(_video.enabled | 0x02u); // Bit 1 of VIC+&15
  }

  void ApplyHideAllSprites(VideoState& _video) noexcept
  {
    // The enable byte zeroed, and nothing else. The positions and colours are
    // left alone, which is why a sprite switched back on reappears where it was rather than at
    // the origin; a port that cleared them here would move the sights every time a view changed.
    _video.enabled = 0;
  }

} // namespace Elite
