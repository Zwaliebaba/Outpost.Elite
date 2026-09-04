#pragma once

#include "ShipSlot.h"

#include <array>
#include <cstdint>

namespace Elite
{

  /*
   * The ship line heap (slice 3b).
   *
   * 6502: the region between `SLSP` and `LS%`. Every ship on screen owns a run of bytes in it,
   * holding the lines that were last drawn for that ship -- a count, then four bytes per line. The
   * game draws by EOR, so drawing the same lines again erases them, and that is the ONLY way a ship
   * is ever removed from the screen. Lose the heap and the game cannot rub anything out.
   *
   * It is addressed absolutely rather than per ship. `XX19` -- which is `INWK+33/34`, the ship's own
   * two bytes -- is a pointer into it, `NWSHP` carves a new run off the bottom by moving `SLSP`
   * down, and `KILLSHP` shuffles every run above a dead ship DOWN by that ship's length. None of
   * that is expressible as an array per ship, which is why this is a region and an address.
   *
   * It is NOT part of `Bubble`, even though its contents are bubble state. `Bubble` holds `SLSP`,
   * because `NWSHP` moves it and refuses a ship when it would collide with the block it is about to
   * write; the bytes live here, and the caller owns both. That keeps the drawing code out of
   * `ShipSlot.h` and lets this header include it rather than the other way round.
   *
   * The region covers the whole arena from `K%` to `LS%` and not just the part a heap can occupy,
   * so that an address is an index and nothing has to be rebased. The bottom of it is where the
   * data blocks are, which `Bubble::blocks` holds instead -- so those bytes are stored twice and
   * this copy of them is never read. `NWSHP`'s refusal is what guarantees the two never describe
   * the same byte, and it is the reason that check was worth porting exactly in slice 3a.
   */
  class LineHeap
  {
  public:
    /// The arena's bounds, from `ShipSlot.h`: blocks grow up from `K%`, heaps grow down from `LS%`.
    static constexpr std::uint16_t BASE = SHIP_BLOCK_BASE;
    static constexpr std::uint16_t TOP = SHIP_HEAP_TOP;
    static constexpr std::size_t SIZE = TOP - BASE;

    /// An address outside the arena reads as zero and writes nowhere, which is what `ShipByte`
    /// does for the blueprints and for the same reason: the original would read whatever was
    /// there, and inventing a value is less honest than reading a zero the caller can see.
    [[nodiscard]] std::uint8_t Read(std::uint16_t _address) const noexcept
    {
      const std::uint32_t offset = static_cast<std::uint32_t>(_address) - BASE;
      return (offset < SIZE) ? m_bytes[offset] : std::uint8_t{0};
    }

    void Write(std::uint16_t _address, std::uint8_t _value) noexcept
    {
      const std::uint32_t offset = static_cast<std::uint32_t>(_address) - BASE;
      if (offset < SIZE)
      {
        m_bytes[offset] = _value;
      }
    }

  private:
    std::array<std::uint8_t, SIZE> m_bytes{};
  };

  /// 6502: XX19(1 0), which shares its location with `INWK+33/34` -- the ship's own heap pointer.
  [[nodiscard]] constexpr std::uint16_t ShipHeapAddress(const ShipBlock& _ship) noexcept
  {
    return static_cast<std::uint16_t>(_ship[SHIP_HEAP_LOW_OFFSET] | (_ship[SHIP_HEAP_HIGH_OFFSET] << 8));
  }

} // namespace Elite
