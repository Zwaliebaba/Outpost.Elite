#include "pch.h"

#include "Raster.h"

#include "LookupTables.h"

namespace Elite
{

  RasterRegisters TickRasterInterrupt(ScreenState& _screen, std::uint8_t _bomb) noexcept
  {
    /*
     * 6502: the whole handler is indexed by RASTCT, and the index is 0 or 1 and nothing else.
     *
     * `innersec` holds 1 and 0, so the only value the handler ever writes back is the other one,
     * and `COLD` starts it at zero. The original would read past the end of every table for any
     * larger value; the port folds instead of inventing that, because a byte the game cannot
     * produce is not behaviour to reproduce.
     */
    const std::size_t index = (_screen.rasterCounter != 0u) ? 1u : 0u;
    const bool spaceView = (index == 0u);

    RasterRegisters out{};
    out.spaceView = spaceView;

    // 6502: `zebop` indexed by the counter -- which block of screen RAM colours this half.
    out.memoryPointers = spaceView ? RASTER_MEMORY_SPACE_VIEW : _screen.colourBank;

    // 6502: `moonflower` into the second control register -- bit 4 is the multicolour bit, so
    // this is the register the energy bomb changes.
    out.control2 = spaceView ? _screen.upperBitmapMode : _screen.bitmapMode;

    // 6502: `shango` into the raster compare -- where the NEXT interrupt fires, which is what
    // alternates.
    out.nextRasterLine = RASTER_NEXT_LINE_TABLE[index];

    /*
     * 6502: `santana` into the sprite multicolour register and `lotus` into sprite 1's colour.
     *
     * The pair that clips the explosion. `santana` is %11111110 for the space view and %11111100
     * for the dashboard, so sprite 1 alone changes mode across the split; `lotus` is 2 and 0, and
     * VIC+&28 is sprite 1's own colour register. Multicolour in red above, single-colour in colour
     * 0 below -- and a single-colour sprite in colour 0 draws nothing.
     */
    out.spriteMulticolour = RASTER_SPRITE_MULTICOLOUR_TABLE[index];
    out.explosionColour = RASTER_SPRITE_COLOUR_TABLE[index];

    /*
     * 6502: the energy bomb's top bit decides whether `welcome` is incremented.
     *
     * ON EVERY PASS, not once a frame -- the test is above the split, so a burning bomb increments
     * the colour twice per frame and the space view flashes at twice the frame rate. `BOMBOFF`
     * puts `welcome` back to zero, and the test reads bit 7, which is the bit the bomb sets.
     */
    if ((_bomb & BOMB_RUNNING) != 0u)
    {
      _screen.backgroundFlash = static_cast<std::uint8_t>(_screen.backgroundFlash + 1u);
    }

    // 6502: `welcome` indexed by the counter into the background register -- the space view's
    // background is the byte just incremented and the dashboard's is its partner, which nothing
    // ever writes.
    out.background = spaceView ? _screen.backgroundFlash : RASTER_BACKGROUND_DASHBOARD;

    // 6502: `innersec` indexed by the counter, written back as the next counter.
    _screen.rasterCounter = RASTER_NEXT_COUNTER_TABLE[index];

    return out;
  }

} // namespace Elite
