#pragma once

#include <cstdint>

namespace Elite
{

  /*
   * SETL1, L1M and `l1` -- the 6510's own input/output port register (slice M3-b-3a).
   *
   * IT IS NOT SELF-MODIFYING CODE AND IT IS NOT IN AN INTERRUPT HANDLER, which is what the port
   * believed for six slices. The conversion plan's row said "`SETL1` is NOT one of them: it is
   * self-modifying code inside a raster interrupt handler and belongs behind a seam like the
   * sound", `Controls.h` repeated it, and `VideoState.h` gave it as the reason this byte could
   * never live here. The routine is EIGHT INSTRUCTIONS and none of them writes code: disable
   * interrupts, store the requested mode, read the processor port, mask off its low three bits,
   * OR the mode in, store it back, re-enable interrupts, return.
   *
   * `l1` is address &0001, which on a 6510 is the processor's own port -- bits 0 to 2 are LORAM,
   * HIRAM and CHAREN, and they decide what the 64K address space holds. The `SEI`/`CLI` bracket is
   * there because an interrupt taken with the map half-written would fetch its vector from the
   * wrong place, and that bracket is what the "interrupt handler" reading was built on.
   *
   * SO IT IS TWO BYTES OF MEMORY, and the same argument that brought `SoundBuffer` in for M3-b-2a
   * and `MusicPlayer` for M3-b-2b brings these: the game writes them, and what a port does about
   * the banking they describe is the port's business. The executable kept `SETL1` as a byte in
   * `FlightSession` that nothing read; this is that byte, in the library, where the routines that
   * write it live.
   *
   * WHAT IT BANKS IS NOT ONLY THE VIC-II, which is why this is not part of `VideoState`. The
   * fourteen callers in the shipped source bracket the sprite registers (`SIGHT`, `PTCLS2`,
   * `MVTRIBS`, `NOSPRITES`, the flight loop's part 15), the SID (`startbd` and `stopat`), the
   * keyboard's CIA (`RDKEY`, `DKSANYKEY`) and the KERNAL's disk routines (`SVE`, `LOD`,
   * `KERNALSETUP`, `COLD`). One register, four chips.
   *
   * WHAT IT IS WORTH, said plainly: the port has no banking, so nothing downstream reads these two
   * bytes and no pixel or sound depends on them. What they buy is that the calls are COMPARABLE --
   * `L1M` and `l1` are ordinary addresses in the oracle's image -- where a write-only seam could
   * only be counted. The harness slice that made them mean something was §6.108's, and it is
   * M6-0-a-1: `Cpu6502` routes a store to &D000-&DFFF to a register file instead of to RAM when
   * bit 2 of `l1` says the I/O page is mapped in, which is what let `ShipDrawEffects` go.
   */

  /// The two values the game ever passes to `SETL1`. %101 maps the I/O page in over the RAM
  /// at &D000-&DFFF so the chips can be reached; %100 maps it back out to RAM.
  inline constexpr std::uint8_t MEMORY_MAP_IO = 0b101;
  inline constexpr std::uint8_t MEMORY_MAP_RAM = 0b100;

  struct MemoryMap
  {
    /*
     * "temporary storage for the new value", and it is assembled as %100.
     *
     * It outlives the call: `SETL1` writes it and then reads it back one instruction later, so a
     * caller sees the last value anybody asked for. Nothing else in the game reads it, which makes
     * it exactly the kind of byte §6.95 warns about -- a scratch location whose initial value is
     * part of the shipped image -- so the port assembles it the same way.
     */
    std::uint8_t requested = MEMORY_MAP_RAM;

    /*
     * The port register itself, at &0001.
     *
     * `SETL1` writes only its bottom three bits and leaves bits 3 to 7 as it found them, because
     * those are the datasette motor and sense lines and the game has no business with them. The
     * port keeps the whole byte for that reason: an `AND #%11111000` that had nothing to preserve
     * would not have been written.
     */
    std::uint8_t port = 0;
  };

  /*
   * `SETL1` itself -- put `_mode`'s bottom three bits into the port register.
   *
   * The `SEI`/`CLI` is not modelled and cannot be: the port has no interrupt to disable, and the
   * pair exists to make the two-instruction read-modify-write atomic against one. In C++ the
   * routine IS atomic, which is the same guarantee arrived at for free.
   */
  void SetMemoryMap(MemoryMap& _map, std::uint8_t _mode) noexcept;

} // namespace Elite
