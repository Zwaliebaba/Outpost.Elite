#pragma once

#include "LookupTables.h"
#include "ShipType.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Elite
{

  /*
   * The ship blueprints, parsed (Design/Modernize.md slice M1-e).
   *
   * `XX21` is a table of thirty-three pointers into the region under it, and each pointer
   * lands on a twenty-byte header followed by the vertices, the edges and the faces. Until this
   * slice the port held the region as bytes addressed absolutely -- `ShipByte(XX0 + 5)` was the
   * heap size -- for the reason slice 3a gave: three of the thirty-three headers disagree with the
   * layout about where their tables end, and a port that cut the region into thirty-three arrays
   * would have had to decide what a blueprint IS. The answer is that a blueprint is its header,
   * named, and three SPANS over the region sized by that header and placed where its offsets say
   * -- which is what the original reads, and the disagreement turns out to be two ships whose
   * edge tables are another ship's, reached by an offset that wraps (§8, M1-e).
   *
   * `E%`, `KWL%` and `KWH%` -- the default `NEWB`, and what a kill is worth -- are one byte per
   * TYPE beside the table, indexed by the type in a slot and not by the blueprint it was drawn
   * with, so they are functions of a `ShipType` and not fields of a `Blueprint`: the station is
   * type 2 whichever of its two blueprints `NWSPS` gave it.
   */

  /// The address `XX21` sits at. Every address in the region, and every address `XX21`
  /// holds, is an offset from here -- kept for the bridge and the tests, which address the
  /// oracle's copy, and read by no routine.
  inline constexpr std::uint16_t SHIP_DATA_BASE = 0xD000;

  /*
   * How many ship types this build carries.
   *
   * The upstream source states it in one line, `NTY=33:D%=&D000:E%=D%+2*NTY`, which settles three
   * things at once: the pointer table is 33 entries, the region starts at `D%`, and `E%` begins
   * immediately after the table.
   */
  inline constexpr std::uint8_t SHIP_TYPE_COUNT = 33;

  /// One byte per ship type, the default `NEWB` flags `NWSHP` ORs into a new ship.
  /// Derived as the source derives it, so the two cannot drift apart.
  inline constexpr std::uint16_t SHIP_DEFAULT_FLAGS = SHIP_DATA_BASE + 2 * SHIP_TYPE_COUNT;

  /// KWL% and KWH% -- what killing a ship type is worth, as a fraction and as whole kills,
  /// one byte per type after `E%`, indexed from ONE like the pointer table.
  inline constexpr std::uint16_t SHIP_KILL_FRACTION = SHIP_DEFAULT_FLAGS + SHIP_TYPE_COUNT;
  inline constexpr std::uint16_t SHIP_KILL_INTEGER = SHIP_KILL_FRACTION + SHIP_TYPE_COUNT;

  /*
   * The blueprint header -- THE WIRE FORMAT `ParseBlueprint` reads, TWENTY bytes because that
   * is what indexes it: the C64 build reads `(XX0),Y` for every Y from 0 to 19 and never higher.
   */
  inline constexpr std::uint8_t SHIP_HEADER_SIZE = 20;
  inline constexpr std::uint8_t SHIP_HEADER_CARGO = 0;             ///< (XX0),0 -- top nibble: what it is worth scooped
  inline constexpr std::uint8_t SHIP_HEADER_TARGET_AREA_LOW = 1;   ///< (XX0),1 -- the targetable area, low byte
  inline constexpr std::uint8_t SHIP_HEADER_TARGET_AREA_HIGH = 2;
  inline constexpr std::uint8_t SHIP_HEADER_EDGES_LOW = 3;         ///< (XX0),3 and 16 -- the edges, as an offset from XX0
  inline constexpr std::uint8_t SHIP_HEADER_FACES_LOW = 4;         ///< (XX0),4 and 17 -- the faces, likewise
  inline constexpr std::uint8_t SHIP_HEADER_HEAP_BYTES = 5;        ///< (XX0),5 -- line heap bytes needed
  inline constexpr std::uint8_t SHIP_HEADER_LASER_VERTEX = 6;      ///< (XX0),6 -- the gun vertex, times four
  inline constexpr std::uint8_t SHIP_HEADER_EXPLOSION_COUNT = 7;   ///< (XX0),7 -- vertices in the explosion cloud
  inline constexpr std::uint8_t SHIP_HEADER_VERTEX_BYTES = 8;      ///< (XX0),8 -- six per vertex
  inline constexpr std::uint8_t SHIP_HEADER_EDGE_COUNT = 9;        ///< (XX0),9 -- four bytes each
  inline constexpr std::uint8_t SHIP_HEADER_BOUNTY_LOW = 10;       ///< (XX0),10 and 11 -- the bounty, low byte first
  inline constexpr std::uint8_t SHIP_HEADER_FACE_BYTES = 12;       ///< (XX0),12 -- four per face
  inline constexpr std::uint8_t SHIP_HEADER_VISIBILITY = 13;       ///< (XX0),13 -- past this z, a dot will do
  inline constexpr std::uint8_t SHIP_HEADER_MAX_ENERGY = 14;
  inline constexpr std::uint8_t SHIP_HEADER_MAX_SPEED = 15;
  inline constexpr std::uint8_t SHIP_HEADER_EDGES_HIGH = 16;
  inline constexpr std::uint8_t SHIP_HEADER_FACES_HIGH = 17;
  inline constexpr std::uint8_t SHIP_HEADER_NORMAL_SHIFTS = 18;    ///< (XX0),18 -- the scaling of the face normals
  inline constexpr std::uint8_t SHIP_HEADER_WEAPONS = 19;          ///< (XX0),19 -- laser power in bits 3 to 7, missiles in 0 to 2

  /*
   * One entry of XX21 and the twenty bytes it points at -- a blueprint, parsed.
   *
   * The header is named; the three tables are spans over the region, sized by the header and
   * starting where the header's offsets say -- which for the Splinter and the Thargon is BEFORE
   * the blueprint, because the offset is sixteen-bit arithmetic that wraps and their edges are
   * another ship's (§8, M1-e). `address` is
   * where `XX21` pointed, for the bridge and the tests; no routine reads it. `NO_BLUEPRINT` is
   * what `XX0` holds before anything has set it: twenty zero bytes, which is what the old
   * `ShipByte` returned for an address outside the region.
   */
  struct Blueprint
  {
    ShipType type = ShipType::None;      ///< the type whose `XX21` entry this is (the Dodo's is 33)
    std::uint16_t address = 0;           ///< What XX21 holds for the type -- the bridge's and the tests'
    std::uint8_t cargo = 0;              ///< (XX0),0 -- `oily` reads the top nibble as the item it is worth scooped
    std::uint16_t targetArea = 0;        ///< (XX0),1 and 2 -- what `HITCH` compares the aim against, low byte first
    std::uint16_t edgesOffset = 0;       ///< (XX0),3 and 16 -- where the edges start, from XX0, in arithmetic that wraps
    std::uint16_t facesOffset = 0;       ///< (XX0),4 and 17 -- where the faces start, likewise
    std::uint8_t heapBytes = 0;          ///< (XX0),5 -- the line heap `NWSHP` carves for it
    std::uint8_t laserVertex = 0;        ///< (XX0),6 -- the gun's vertex, times four, an index into XX3
    std::uint8_t explosionCount = 0;     ///< (XX0),7 -- how many vertices `DOEXP` bursts from
    std::uint8_t vertexBytes = 0;        ///< (XX0),8 -- six per vertex
    std::uint8_t edgeCount = 0;
    std::uint16_t bounty = 0;            ///< (XX0),10 and 11 -- in tenths, low byte first
    std::uint8_t faceBytes = 0;          ///< (XX0),12 -- four per face
    std::uint8_t visibility = 0;         ///< (XX0),13 -- past this z high byte, `LL13` draws a dot
    std::uint8_t maxEnergy = 0;
    std::uint8_t maxSpeed = 0;
    std::uint8_t normalShifts = 0;       ///< (XX0),18 -- how far `LL9` scales the normals down
    std::uint8_t weapons = 0;            ///< (XX0),19 -- laser power in bits 3 to 7, missiles in bits 0 to 2

    std::span<const std::uint8_t> vertices{}; ///< XX0+20 onward, `vertexBytes` long -- x, y, z, flags, and two faces each
    std::span<const std::uint8_t> edges{};    ///< XX0 plus `edgesOffset`, four per edge -- distance, faces, and two vertices
    std::span<const std::uint8_t> faces{};    ///< XX0 plus `facesOffset`, four per face -- flags and a normal

    /// How many bytes the blueprint says it occupies: the header, then its three tables. It
    /// disagrees with the distance to the next blueprint for three of the thirty-three, which
    /// `ShipDataTests` states and nothing in the port depends on.
    [[nodiscard]] constexpr std::uint16_t Extent() const noexcept
    {
      return static_cast<std::uint16_t>(SHIP_HEADER_SIZE + vertexBytes + 4u * edgeCount + faceBytes);
    }
  };

  /// XX0 before anything has pointed it at a ship -- twenty bytes of zero.
  inline constexpr Blueprint NO_BLUEPRINT{};

  /*
   * The two bytes at `XX21` indexed by twice the type -- the blueprint for a ship type, or
   * null if this build does not carry one.
   *
   * The off-by-one in the original is the indexing, not a bug: `XX21` is indexed from ONE, because
   * ship type 0 means an empty slot. `NWSHP` checks the high byte for zero and refuses the ship,
   * which is how a build that omits a ship type behaves when something asks for it. A type above
   * `SHIP_TYPE_COUNT` is null rather than a read past the table, which nothing in the game asks for.
   */
  [[nodiscard]] const Blueprint* BlueprintOf(ShipType _shipType) noexcept;

  /// The blueprint `XX21` points at an address -- `NO_BLUEPRINT` for zero, null for an address
  /// that is not a blueprint's. The bridge's way from the oracle's `XX0` back to a pointer.
  [[nodiscard]] const Blueprint* BlueprintAt(std::uint16_t _address) noexcept;

  /// The default `NEWB` for a type, read one below `E%`. The NEGATIVE types reach it too
  /// (`NW2` falls into `NW8`), and read past the thirty-three entries into the kill tables and
  /// the first blueprint: a defined byte, reproduced.
  [[nodiscard]] std::uint8_t DefaultNewbFor(ShipType _shipType) noexcept;

  /// KWL%-1,X and KWH%-1,X -- what killing a type is worth: a fraction of a kill, and whole
  /// kills, which `EXNO2` adds to `TALLYL` and `TALLY` with the carry between them.
  struct KillWorth
  {
    std::uint8_t fraction = 0;
    std::uint8_t whole = 0;
  };
  [[nodiscard]] KillWorth KillWorthFor(ShipType _shipType) noexcept;

} // namespace Elite
