#include "pch.h"

#include "VideoState.h"

namespace Elite
{

  void ApplySightColour(VideoState& _video, std::uint8_t _colour) noexcept
  {
    _video.colour[0] = _colour; // 6502: STA VIC+&27
  }

  void ApplySpritesEnabled(VideoState& _video, std::uint8_t _mask) noexcept
  {
    _video.enabled = _mask; // 6502: STA VIC+&15
  }

  void ApplyMaskSprites(VideoState& _video, std::uint8_t _mask) noexcept
  {
    // 6502: LDA VIC+&15 / AND #%00000011 / STA VIC+&15 -- and the READ is the point. The flight
    // loop's part 15 does not know how many Trumble sprites are showing, so it cannot compute the
    // new byte; it masks whatever is there.
    _video.enabled = static_cast<std::uint8_t>(_video.enabled & _mask);
  }

  void ApplySpriteExpansion(VideoState& _video, std::uint8_t _mask) noexcept
  {
    _video.expanded = _mask; // 6502: STA VIC+&17 / STA VIC+&1D
  }

  void ApplyExplosionSprite(VideoState& _video, std::uint16_t _x, std::uint8_t _y) noexcept
  {
    /*
     * 6502: the five writes that place sprite 1 and switch it on.
     *
     * The nine-bit x arrives whole. On the hardware the low eight go to VIC+&2 and the ninth is
     * bit 1 of VIC+&10, set with `LDA VIC+&10 / AND #%11111101 / ORA exlook,X` -- a two-byte table
     * whose only job is to shift a 0 or 1 left one place. Keeping it whole here is not a
     * simplification: the split is the register layout's, and the one place that has to imitate a
     * register is the presenter.
     */
    _video.x[1] = _x;
    _video.y[1] = _y;
    _video.enabled = static_cast<std::uint8_t>(_video.enabled | 0x02u); // 6502: bit 1 of VIC+&15
  }

  void ApplyHideAllSprites(VideoState& _video) noexcept
  {
    // 6502: NOSPRITES -- `LDA #0 / STA VIC+&15`, and nothing else. The positions and colours are
    // left alone, which is why a sprite switched back on reappears where it was rather than at the
    // origin; a port that cleared them here would move the sights every time a view changed.
    _video.enabled = 0;
  }

} // namespace Elite
