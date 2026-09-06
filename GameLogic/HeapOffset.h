#pragma once

#include <cstdint>

namespace Elite
{

  /*
   * Where the ship line heap lives, and how a place in it is named (Design/Modernize.md M1-f).
   *
   * 6502: K% and LS% -- the blocks grow UP from `K%` and the heap grows DOWN from `LS%`, and `NWSHP`
   * refuses a ship when the heap it would need runs down into the block it is about to write. That
   * check compares two ADDRESSES, the new heap bottom against `INF`, and it is real and reachable:
   * a bubble full of complex ships runs out of heap before it runs out of slots. So the two
   * constants stay, as the only 6502 addresses left in the model: `Bubble::TryReserveHeap` does the
   * comparison with them, and `HeapOffset::FromAddress` and `Address` are how the wire format --
   * `INWK+33/34`, `SLSP`, the oracle's copy -- crosses to and from an offset.
   */
  inline constexpr std::uint16_t SHIP_BLOCK_BASE = 0xF900; ///< 6502: K%
  inline constexpr std::uint16_t SHIP_HEAP_TOP = 0xFFC0;   ///< 6502: LS%, where SLSP starts

  /// 6502: LSO -- the SUN's line heap, which `NWSPS` hands to the space station: nowhere near the
  /// arena, and the reason `LineHeap` takes a window on loan (§6.112).
  inline constexpr std::uint16_t SUN_HEAP_ADDRESS = 1408;

  /*
   * 6502: XX19(1 0) and SLSP -- a place in the ship line heap, as bytes UP from `K%` (ruling R-c: the
   * arena is addressed by offset).
   *
   * A strong type with no implicit conversion: `Byte(Y)` is `(XX19),Y`, the Y-th byte of a ship's
   * run; `Back(n)` is `KILLSHP` moving a run down by the dead ship's length; `Address` and
   * `FromAddress` are for the wire format and nothing else. The DEFAULT is address 0 -- what `ZINF`
   * writes to `INWK+33/34` and what a block holds before `NWSHP` reaches it -- which is outside
   * the arena, as the original's zero pointer is; `Top()` is `LS%`, where `SLSP` starts. The
   * station's pointer, `FromAddress(SUN_HEAP_ADDRESS)`, is outside the arena too, and `LineHeap`
   * resolves it against the window it has been lent.
   */
  struct HeapOffset
  {
    std::uint16_t up = static_cast<std::uint16_t>(0u - SHIP_BLOCK_BASE);

    [[nodiscard]] static constexpr HeapOffset FromAddress(std::uint16_t _address) noexcept
    {
      return {static_cast<std::uint16_t>(_address - SHIP_BLOCK_BASE)};
    }
    [[nodiscard]] static constexpr HeapOffset Top() noexcept
    {
      return FromAddress(SHIP_HEAP_TOP);
    }

    [[nodiscard]] constexpr std::uint16_t Address() const noexcept
    {
      return static_cast<std::uint16_t>(up + SHIP_BLOCK_BASE);
    }
    /// 6502: (XX19),Y -- the Y-th byte of the run that starts here.
    [[nodiscard]] constexpr HeapOffset Byte(std::uint16_t _index) const noexcept
    {
      return {static_cast<std::uint16_t>(up + _index)};
    }
    /// 6502: SLSP moving down, or `KILLSHP`'s `P` -- a run that starts this many bytes lower.
    [[nodiscard]] constexpr HeapOffset Back(std::uint16_t _bytes) const noexcept
    {
      return {static_cast<std::uint16_t>(up - _bytes)};
    }

    [[nodiscard]] constexpr bool operator==(const HeapOffset&) const noexcept = default;
  };

  static_assert(HeapOffset{}.Address() == 0u, "the default is the zero pointer ZINF writes");
  static_assert(HeapOffset::Top().Address() == SHIP_HEAP_TOP && HeapOffset::FromAddress(SHIP_BLOCK_BASE).up == 0u, "the arena's ends");

} // namespace Elite
