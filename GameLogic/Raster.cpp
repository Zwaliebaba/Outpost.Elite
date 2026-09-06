#include "pch.h"

#include "Raster.h"

#include "LookupTables.h"

namespace Elite
{

  RasterRegisters TickRasterInterrupt(ScreenState& _screen, std::uint8_t _bomb) noexcept
  {
    /*
     * 6502: LDX RASTCT -- and the index is 0 or 1 and nothing else.
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

    // 6502: LDA zebop,X / STA VIC+&18 -- which block of screen RAM colours this half.
    out.memoryPointers = spaceView ? RASTER_MEMORY_SPACE_VIEW : _screen.colourBank;

    // 6502: LDA moonflower,X / STA VIC+&16 -- and bit 4 is the multicolour bit, so this is the
    // register the energy bomb changes.
    out.control2 = spaceView ? _screen.upperBitmapMode : _screen.bitmapMode;

    // 6502: LDA shango,X / STA VIC+&12 -- where the NEXT interrupt fires, which is what alternates.
    out.nextRasterLine = RASTER_NEXT_LINE_TABLE[index];

    // 6502: LDA santana,X / STA VIC+&1C and LDA lotus,X / STA VIC+&28.
    out.spriteMulticolour = RASTER_SPRITE_MULTICOLOUR_TABLE[index];
    out.spriteColour = RASTER_SPRITE_COLOUR_TABLE[index];

    /*
     * 6502: BIT BOMB / BPL nobombef / INC welcome.
     *
     * ON EVERY PASS, not once a frame -- the test is above the split, so a burning bomb increments
     * the colour twice per frame and the space view flashes at twice the frame rate. `BOMBOFF` puts
     * `welcome` back to zero and `BIT` reads bit 7, which is the bit the bomb sets.
     */
    if ((_bomb & BOMB_RUNNING) != 0u)
    {
      _screen.backgroundFlash = static_cast<std::uint8_t>(_screen.backgroundFlash + 1u);
    }

    // 6502: LDA welcome,X / STA VIC+&21 -- the space view's background is the byte just
    // incremented and the dashboard's is its partner, which nothing ever writes.
    out.background = spaceView ? _screen.backgroundFlash : RASTER_BACKGROUND_DASHBOARD;

    // 6502: LDA innersec,X / STA RASTCT.
    _screen.rasterCounter = RASTER_NEXT_COUNTER_TABLE[index];

    return out;
  }

} // namespace Elite
