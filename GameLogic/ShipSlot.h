#pragma once

#include "ShipBlueprint.h"

#include <array>
#include <cstdint>

namespace Elite
{

/*
 * The local bubble of ships (slice 3a).
 *
 * 6502: FRIN, K%, UNIV and MANY. Elite does not have a world; it has a BUBBLE of at most ten
 * ships around the player, created as they come into range and destroyed as they leave. Every
 * routine that moves, draws, shoots at or is shot by a ship works on one slot of this at a time,
 * copied into `INWK` and copied back.
 */

/// 6502: NI% -- how many bytes one ship's data block is. Thirty-seven, not thirty-six: the
/// resolved C64 source says `NI% = 37`, and the `original-sources` listings disagree with the
/// library on other constants, which is what `tools/c64_source.py` exists to settle.
inline constexpr std::uint8_t SHIP_BLOCK_SIZE = 37;

/*
 * 6502: NOSH -- the most ships the bubble holds at once.
 *
 * Ten, and the assembled layout says so independently of the source: `FRIN` is at 1106 and `MANY`
 * at 1117, eleven bytes apart, which is `NOSH + 1` for the terminator. Worth the cross-check
 * because the raw `original-sources` listings carry both 10 and 20 for this name -- they serve
 * several versions of the game -- so grepping them gives whichever comes first.
 */
inline constexpr std::uint8_t MAX_SHIPS = 10;

/*
 * 6502: INWK, and one entry of K% -- a single ship, as thirty-seven bytes.
 *
 * BYTES AND NOT FIELDS, deliberately. The original addresses this block by offset from three
 * different directions -- `INWK,X` and `INWK+10,Y` walk it as vectors, `MVS4` steps Y through it
 * in sixes, and `NWSHP` copies it wholesale through `(INF),Y` -- and the same byte is a
 * coordinate to one routine and half a rotation matrix to the next. A struct would have to pick
 * one reading and would make the other two into casts.
 *
 * Named offsets go on top of it as the routines that use them are ported, which is how
 * `CommanderBlock` is handled and for the same reason (ADR-002 §3).
 */
struct ShipBlock
{
  std::array<std::uint8_t, SHIP_BLOCK_SIZE> bytes{};

  [[nodiscard]] std::uint8_t& operator[](std::size_t _offset) noexcept { return bytes[_offset]; }
  [[nodiscard]] std::uint8_t operator[](std::size_t _offset) const noexcept { return bytes[_offset]; }
};

/*
 * 6502: FRIN, K% and MANY together -- everything that is in the bubble right now.
 *
 * `UNIV` has no equivalent and needs none: it is a table of POINTERS to the ten blocks in `K%`,
 * which exists because the 6502 has no way to multiply an index by 37 cheaply. `GINF` reads it to
 * turn a slot number into an address. Here the blocks are an array and the index is the index, so
 * the table is the one piece of the original this port replaces rather than reproduces -- and the
 * replacement is exact, because nothing else ever reads `UNIV`.
 */
struct Bubble
{
  /// 6502: FRIN -- the ship type in each slot, zero for empty, and one byte more than there are
  /// slots because the scan for a free one runs off the end and stops on the terminator.
  std::array<std::uint8_t, MAX_SHIPS + 1> slots{};

  /// 6502: K% -- the ten data blocks the slots point at.
  std::array<ShipBlock, MAX_SHIPS> blocks{};

  /*
   * 6502: MANY -- how many of each type are in the bubble, indexed by SHIP TYPE.
   *
   * Sized by what indexes it (§6.8): `INC MANY,X` with X a ship type, so types 0 to
   * `SHIP_TYPE_COUNT` inclusive. Entry 0 is never incremented -- type 0 means an empty slot --
   * and is kept so the index is the type rather than the type minus one.
   */
  std::array<std::uint8_t, SHIP_TYPE_COUNT + 1u> counts{};

  /// 6502: JUNK -- cargo canisters, escape pods and the rest, counted together as well as
  /// separately, because the tactics code asks "is any of this worth shooting at".
  std::uint8_t junk = 0;
};

/*
 * 6502: GINF -- the address of slot X's data block.
 *
 * `TXA / ASL A / TAY / LDA UNIV,Y / STA INF / LDA UNIV+1,Y / STA INF+1 / RTS`, which is a
 * doubling and a table read because the 6502 cannot index by 37. Here it is the index, and the
 * routine survives as a named function only because the ledger counts it and because a caller
 * that asked for slot 10 in the original would read past `UNIV`.
 */
[[nodiscard]] ShipBlock* SlotBlock(Bubble& _bubble, std::uint8_t _slot) noexcept;

} // namespace Elite
