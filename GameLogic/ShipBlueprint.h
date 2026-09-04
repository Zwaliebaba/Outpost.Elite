#pragma once

#include "LookupTables.h"

#include <cstdint>

namespace Elite
{

  /*
   * Reading a ship's blueprint (slice 3a).
   *
   * The blueprints are one region of bytes addressed absolutely, for the reasons `SHIP_DATA`'s
   * declaration gives, so this is the addressing rather than a parsed structure. That is deliberate
   * and it is ADR-001's rule rather than laziness: `NWSHP` reads byte 5, `MVEIT` byte 15, `LL9` the
   * vertices through a pointer it advances itself, and a port that turned the region into structs
   * would have to decide what a blueprint IS -- a question the original never answers and two of
   * the thirty-three would answer differently from the rest.
   */

  /// 6502: the address `XX21` sits at. Every address in the region, and every address `XX21`
  /// holds, is an offset from here.
  inline constexpr std::uint16_t SHIP_DATA_BASE = 0xD000;

  /*
   * 6502: NTY -- how many ship types this build carries.
   *
   * The upstream source states it in one line, `NTY=33:D%=&D000:E%=D%+2*NTY`, which settles three
   * things at once: the pointer table is 33 entries, the region starts at `D%`, and `E%` begins
   * immediately after the table. It is worth having as a constant because reading past the table
   * does not fault -- it reads `E%`'s bytes and returns them as addresses, and they look plausible
   * enough to chase. Entries 35, 38 and 39 come out as 1, 24865 and 41120.
   */
  inline constexpr std::uint8_t SHIP_TYPE_COUNT = 33;

  /// 6502: E% -- one byte per ship type, the default `NEWB` flags `NWSHP` ORs into a new ship.
  /// Derived as the source derives it, so the two cannot drift apart.
  inline constexpr std::uint16_t SHIP_DEFAULT_FLAGS = SHIP_DATA_BASE + 2 * SHIP_TYPE_COUNT;

  /*
   * 6502: KWL% and KWH% -- what killing a ship type is worth, as a fraction and as whole kills.
   *
   * Derived the way the source derives them rather than written as addresses: `E%` is one byte per
   * type and each table is one byte per type after it, so the four constants are one chain and a
   * build with a different `NTY` moves them all together. Both are indexed from ONE, like the
   * pointer table -- `EXNO2` reads `KWL%-1,X` with X the ship type.
   */
  inline constexpr std::uint16_t SHIP_KILL_FRACTION = SHIP_DEFAULT_FLAGS + SHIP_TYPE_COUNT;
  inline constexpr std::uint16_t SHIP_KILL_INTEGER = SHIP_KILL_FRACTION + SHIP_TYPE_COUNT;

  /*
   * 6502: the blueprint header, and TWENTY bytes because that is what indexes it.
   *
   * Counted from the source rather than taken from the gap to `SHIP_x_VERTICES`: the C64 build
   * reads `(XX0),Y` for every Y from 0 to 19 and never higher, across thirty accesses. The gap
   * happens to agree, which is a check rather than the reason (§6.8).
   */
  inline constexpr std::uint8_t SHIP_HEADER_SIZE = 20;

  /*
   * The three header bytes that describe the blueprint's own extent, named because
   * `ShipBlueprintExtent` below is the only thing that uses all three and a reader should not have
   * to rediscover which byte is which.
   */
  inline constexpr std::uint8_t SHIP_HEADER_VERTEX_BYTES = 8; ///< 6502: (XX0),8 -- six per vertex
  inline constexpr std::uint8_t SHIP_HEADER_EDGE_COUNT = 9;   ///< 6502: (XX0),9 -- four bytes each
  inline constexpr std::uint8_t SHIP_HEADER_FACE_BYTES = 12;  ///< 6502: (XX0),12 -- four per face

  /// 6502: LDA (XX0),Y -- one byte of the ship data region, by ADDRESS. An address outside the
  /// region reads zero rather than running off the end; the game cannot form one, so this is a
  /// guard against a future routine being wrong rather than a clamp that changes behaviour.
  [[nodiscard]] std::uint8_t ShipByte(std::uint16_t _address) noexcept;

  /*
   * 6502: LDA XX21-1,Y / LDA XX21-2,Y with Y = type * 2 -- the blueprint for a ship type, or zero
   * if this build does not carry one.
   *
   * The off-by-one in the original is the indexing, not a bug: `XX21` is indexed from ONE, because
   * ship type 0 means an empty slot. `NWSHP` checks the high byte for zero and refuses the ship,
   * which is how a build that omits a ship type behaves when something asks for it.
   *
   * A type above `SHIP_TYPE_COUNT` returns zero rather than reading past the table. The original
   * has no such check and does not need one -- nothing in the game asks for a type it does not
   * carry -- so this is a guard against a future routine being wrong, in the same spirit as
   * `Canvas::Read`'s bounds check, and not a behaviour the game has.
   */
  [[nodiscard]] std::uint16_t BlueprintAddress(std::uint8_t _shipType) noexcept;

  /*
   * How many bytes a blueprint occupies, from its own header.
   *
   * Twenty of header, then `(XX0),8` bytes of vertices, four times `(XX0),9` of edges and
   * `(XX0),12` of faces. It agrees with the distance to the next blueprint for thirty of the
   * thirty-three; `ShipDataTests` names the three it does not and why, and nothing in the port
   * depends on the agreement -- this exists so that a test can state the disagreement rather than
   * so that a caller can slice the region up.
   */
  [[nodiscard]] std::uint16_t ShipBlueprintExtent(std::uint16_t _blueprint) noexcept;

} // namespace Elite
