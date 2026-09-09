#pragma once

#include <cstdint>

#include "ViewChange.h"

namespace Elite
{

  /*
   * COMIRQ1's VIC-II half -- the raster interrupt that makes the screen two screens.
   *
   * The C64 has one bitmap and Elite needs two: a standard-mode space view coloured from one block
   * of screen RAM, and a multicolour dashboard coloured from another. The VIC-II can only be in one
   * mode at a time, so the game reprograms it TWICE A FRAME from an interrupt that fires on a
   * chosen raster line, and `RASTCT` says which of the two set-ups is going in.
   *
   * The handler reads seven consecutive two-byte tables, indexed by `RASTCT`, and writes six VIC
   * registers from them. Four of the seven are constants (`LookupTables.h`); the other three have
   * a second byte that is a separately named variable something writes, so they are `ScreenState`.
   *
   * WHAT THIS IS FOR, since the port has no interrupts. FOUR of the six registers reach the screen
   * and they are two effects. `moonflower` and `welcome` are the ENERGY BOMB: the first puts the
   * space view into multicolour while the bomb burns and the second is the background colour the
   * handler increments on every pass, which is the flashing. `santana` and `lotus` are the
   * EXPLOSION SPRITE, and together they are how the original keeps it out of the dashboard --
   * multicolour and red above the raster split, single-colour in colour 0 below it, which paints
   * nothing. Not a clip rectangle; a colour change (§6.155).
   *
   * The sound half of the same handler is `RunSoundInterrupt` (§6.129), and the two are deliberately
   * separate: `COMIRQ1` reaches the sound only on the pass that leaves `RASTCT` at zero, and the
   * port's presenter runs the VIC half on both passes and the sound half once, which is the same
   * thing said without a fall-through.
   */

  /// The space view's screen RAM block, and the one entry of that pair the game
  /// never writes. Its partner is `abraxas`, which `wantdials` moves to &91 for the dashboard.
  inline constexpr std::uint8_t RASTER_MEMORY_SPACE_VIEW = 0x81;

  /// The dashboard's background colour, and the other fixed half of a pair. The
  /// upstream comment says it plainly: this byte is never changed, so only the space view flashes.
  inline constexpr std::uint8_t RASTER_BACKGROUND_DASHBOARD = 0x00;

  /// Bit 7 is "the energy bomb is going off", and bit 7 is the only bit read.
  inline constexpr std::uint8_t BOMB_RUNNING = 0x80;

  /// VIC+&16 bit 4 -- the VIC-II's multicolour bit, which is the whole difference between
  /// `moonflower`'s %11000000 and the %11010000 the energy bomb stores.
  inline constexpr std::uint8_t BITMAP_MODE_MULTICOLOUR = 0x10;

  /// The six VIC-II registers one pass of the handler writes, in the order it writes them.
  struct RasterRegisters
  {
    std::uint8_t memoryPointers = 0;     ///< VIC+&18 -- zebop / abraxas
    std::uint8_t control2 = 0;           ///< VIC+&16 -- moonflower / caravanserai
    std::uint8_t nextRasterLine = 0;     ///< VIC+&12 -- shango
    std::uint8_t spriteMulticolour = 0;  ///< VIC+&1C -- santana, which sprites are multicolour
    std::uint8_t explosionColour = 0;    ///< VIC+&28 -- lotus, and &28 is SPRITE 1's colour
    std::uint8_t background = 0;         ///< VIC+&21 -- welcome

    /// True on the pass that sets up the space view, which is `RASTCT` = 0.
    bool spaceView = true;
  };

  /*
   * COMIRQ1 from the load of `RASTCT` to the store back, which is the whole of its VIC-II
   * half.
   *
   * Advances `_screen.rasterCounter` and returns what the pass just written would put on the
   * screen. `_bomb` is `BOMB`, and only its bit 7 is read.
   *
   * WHAT IS NOT HERE AND WHY. The handler opens by mapping the I/O page in (`l1`, the 6510 port
   * register) and acknowledging the interrupt (`VIC+&19`), and closes by mapping it back. Both are
   * plumbing for the mechanism the port does not have -- there is no interrupt to acknowledge and
   * no page to map -- and the first is already the `SetRasterMode` seam. The sound tail is
   * `RunSoundInterrupt`.
   */
  RasterRegisters TickRasterInterrupt(ScreenState& _screen, std::uint8_t _bomb) noexcept;

} // namespace Elite
