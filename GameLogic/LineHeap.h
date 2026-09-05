#pragma once

#include "ShipSlot.h"

#include <array>
#include <cstdint>
#include <span>

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

    /*
     * 6502: LSO -- the SUN's line heap, which the SPACE STATION borrows, and it is nowhere near
     * this arena.
     *
     * `NWSPS` empties the sun's slot and then writes `LDA #LO(LSO) / STA INWK+33`, so the station's
     * heap pointer is &0580 and not an address carved out of `SLSP`. On the machine that is
     * unremarkable -- memory is flat and a pointer is a pointer. In this port the sun's 200 bytes
     * live in `PlanetSunState` and the ship arena lives here, so the station's pointer lands
     * BETWEEN two objects: every line it wrote went out of range and was dropped, `LL9` set the
     * "drawn" bit on a ship that had put nothing on screen, and the station was invisible from the
     * moment you launched (§6.112).
     *
     * So the heap can be given that window, and the two never collide for the reason the game
     * relies on: `NWSPS` evicts the sun before it takes the heap, so a station and a sun are never
     * in the bubble at once. Nothing else in the build points a ship at anything but this arena.
     */
    void AttachSunHeap(std::uint16_t _base, std::span<std::uint8_t> _bytes) noexcept
    {
      m_sunBase = _base;
      m_sun = _bytes;
    }

    /// An address outside the arena reads as zero and writes nowhere, which is what `ShipByte`
    /// does for the blueprints and for the same reason: the original would read whatever was
    /// there, and inventing a value is less honest than reading a zero the caller can see.
    [[nodiscard]] std::uint8_t Read(std::uint16_t _address) const noexcept
    {
      const std::uint32_t borrowed = static_cast<std::uint32_t>(_address) - m_sunBase;
      if (!m_sun.empty() && borrowed < m_sun.size())
      {
        return m_sun[borrowed];
      }

      const std::uint32_t offset = static_cast<std::uint32_t>(_address) - BASE;
      return (offset < SIZE) ? m_bytes[offset] : std::uint8_t{0};
    }

    void Write(std::uint16_t _address, std::uint8_t _value) noexcept
    {
      const std::uint32_t borrowed = static_cast<std::uint32_t>(_address) - m_sunBase;
      if (!m_sun.empty() && borrowed < m_sun.size())
      {
        m_sun[borrowed] = _value;
        return;
      }

      const std::uint32_t offset = static_cast<std::uint32_t>(_address) - BASE;
      if (offset < SIZE)
      {
        m_bytes[offset] = _value;
      }
    }

  private:
    std::array<std::uint8_t, SIZE> m_bytes{};

    /// The sun's heap, if the owner has lent it -- see `AttachSunHeap`. Empty until it does, and
    /// then a window at `m_sunBase` that takes precedence over the arena; the two cannot overlap,
    /// because one is at &0580 and the other ends at &FFC0.
    std::span<std::uint8_t> m_sun{};
    std::uint16_t m_sunBase = 0;
  };

  /// 6502: XX19(1 0), which shares its location with `INWK+33/34` -- the ship's own heap pointer.
  [[nodiscard]] constexpr std::uint16_t ShipHeapAddress(const ShipBlock& _ship) noexcept
  {
    return static_cast<std::uint16_t>(_ship[SHIP_HEAP_LOW_OFFSET] | (_ship[SHIP_HEAP_HIGH_OFFSET] << 8));
  }

} // namespace Elite
