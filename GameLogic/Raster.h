#pragma once

#include <cstdint>

#include "ViewChange.h"

namespace Elite
{

  /*
   * 6502: COMIRQ1's VIC-II half -- the raster interrupt that makes the screen two screens.
   *
   * The C64 has one bitmap and Elite needs two: a standard-mode space view coloured from one block
   * of screen RAM, and a multicolour dashboard coloured from another. The VIC-II can only be in one
   * mode at a time, so the game reprograms it TWICE A FRAME from an interrupt that fires on a
   * chosen raster line, and `RASTCT` says which of the two set-ups is going in.
   *
   * The handler reads seven consecutive two-byte tables at `LDX RASTCT` and writes six VIC
   * registers from them. Four of the seven are constants (`LookupTables.h`); the other three have a
   * second byte that is a separately named variable something writes, so they are `ScreenState`.
   *
   * WHAT THIS IS FOR, since the port has no interrupts. Two of those registers are the energy
   * bomb: `moonflower` puts the SPACE VIEW into multicolour mode while the bomb burns, and
   * `welcome` is the background colour the handler increments on every pass, which is the flashing.
   * Neither is reachable any other way -- both are written by the flight loop and read only here --
   * so without this the port carries the bomb's state and never shows it (§6.155).
   *
   * The sound half of the same handler is `RunSoundInterrupt` (§6.129), and the two are deliberately
   * separate: `COMIRQ1` reaches the sound only on the pass that leaves `RASTCT` at zero, and the
   * port's presenter runs the VIC half on both passes and the sound half once, which is the same
   * thing said without a fall-through.
   */

  /// 6502: zebop -- the space view's screen RAM block, and the one entry of that pair the game
  /// never writes. Its partner is `abraxas`, which `wantdials` moves to &91 for the dashboard.
  inline constexpr std::uint8_t RASTER_MEMORY_SPACE_VIEW = 0x81;

  /// 6502: welcome+1 -- the dashboard's background colour, and the other fixed half of a pair. The
  /// upstream comment says it plainly: this byte is never changed, so only the space view flashes.
  inline constexpr std::uint8_t RASTER_BACKGROUND_DASHBOARD = 0x00;

  /// 6502: BOMB -- bit 7 is "the energy bomb is going off", which is the bit `BIT BOMB / BPL` tests.
  inline constexpr std::uint8_t BOMB_RUNNING = 0x80;

  /// 6502: VIC+&16 bit 4 -- the VIC-II's multicolour bit, which is the whole difference between
  /// `moonflower`'s %11000000 and the %11010000 the energy bomb stores.
  inline constexpr std::uint8_t BITMAP_MODE_MULTICOLOUR = 0x10;

  /// The six VIC-II registers one pass of the handler writes, in the order it writes them.
  struct RasterRegisters
  {
    std::uint8_t memoryPointers = 0;     ///< 6502: VIC+&18 -- zebop / abraxas
    std::uint8_t control2 = 0;           ///< 6502: VIC+&16 -- moonflower / caravanserai
    std::uint8_t nextRasterLine = 0;     ///< 6502: VIC+&12 -- shango
    std::uint8_t spriteMulticolour = 0;  ///< 6502: VIC+&1C -- santana
    std::uint8_t spriteColour = 0;       ///< 6502: VIC+&28 -- lotus
    std::uint8_t background = 0;         ///< 6502: VIC+&21 -- welcome

    /// True on the pass that sets up the space view, which is `RASTCT` = 0.
    bool spaceView = true;
  };

  /*
   * 6502: COMIRQ1 from `LDX RASTCT` to `STA RASTCT`, which is the whole of its VIC-II half.
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
