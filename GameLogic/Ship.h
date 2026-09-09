#pragma once

#include "EliteTypes.h"
#include "HeapOffset.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

/*
 * A ship, as the thing the thirty-seven bytes describe (Design/Modernize.md slice M1-c).
 *
 * INWK, and one entry of K%. Until this slice the port held a ship as `std::array<std::uint8_t,
 * 37>` with accessors over it (M1-a), because the original addresses the block by offset from
 * three directions and the bytes are what the oracle compares. Both are still true and neither
 * needs the storage to be bytes: the routines that walk the block by offset -- `MVT1` with X = 0, 3
 * or 6, `MVS4` with Y = 9, 15 or 21, `MVS5` with two component offsets -- address it through
 * `PositionAt`, `VectorAt` and `ComponentAt` below, which turn the offset into the field it names;
 * and the oracle compares `ToBytes()`, which lays the fields out exactly as `K%` does. The bytes
 * are the WIRE FORMAT -- what the bridge materialises, what a partial copy like `MAL4`'s twenty-nine
 * bytes counts through -- and the struct is the model.
 *
 * Nothing here allocates, throws, or reaches the platform.
 */
namespace Elite
{

  /// How many bytes one ship's data block is. Thirty-seven, not thirty-six: the
  /// resolved C64 source says `NI% = 37`, and the `original-sources` listings disagree with the
  /// library on other constants, which is what `tools/c64_source.py` exists to settle.
  inline constexpr std::uint8_t SHIP_BLOCK_SIZE = 37;

  /*
   * THE LAYOUT OF THE BLOCK -- the wire format `ToBytes` writes and `FromBytes` reads, named once.
   *
   * INWK+0 to INWK+8 are the position, three bytes an axis -- a sixteen-bit magnitude low
   * byte first and a sign byte whose bit 7 is the sign (`EliteTypes.h`'s `SignMag24`). INWK+9 to INWK+26 are the
   * orientation: three vectors of six bytes, `nosev`, `roofv` and `sidev`, each an x, y and z
   * component of two bytes with the sign in bit 7 of the high one (`Vector16`). The routines that
   * are entered with an axis or a vector in a register take the offset as they always did (M2
   * gives them an enumeration) and `PositionAt`, `VectorAt` and `ComponentAt` map it to the field.
   */
  inline constexpr std::uint8_t SHIP_X_OFFSET = 0; ///< INWK+0, +1, +2 -- x_lo, x_hi, x_sign
  inline constexpr std::uint8_t SHIP_Y_OFFSET = 3;
  inline constexpr std::uint8_t SHIP_Z_OFFSET = 6;

  inline constexpr std::uint8_t SHIP_NOSE_OFFSET = 9;  ///< INWK+9 to +14 -- nosev, x lo/hi, y lo/hi, z lo/hi
  inline constexpr std::uint8_t SHIP_ROOF_OFFSET = 15; ///< INWK+15 to +20 -- roofv
  inline constexpr std::uint8_t SHIP_SIDE_OFFSET = 21; ///< INWK+21 to +26 -- sidev

  inline constexpr std::uint8_t SHIP_SPEED_OFFSET = 27;
  inline constexpr std::uint8_t SHIP_ACCELERATION_OFFSET = 28;
  inline constexpr std::uint8_t SHIP_ROLL_OFFSET = 29;         ///< The roll counter
  inline constexpr std::uint8_t SHIP_PITCH_OFFSET = 30;        ///< The pitch counter

  /*
   * One byte holding five things, which is why it does not get a name saying what
   * it is FOR.
   *
   * Slice 3a called this `SHIP_MISSILES_OFFSET`, because `NWSHP` is the only routine that had
   * reached it and all `NWSHP` does is OR the blueprint's missile count into the bottom three bits.
   * The drawing code reads the same byte for something else entirely -- bit 3 says whether the ship
   * is currently on the screen, and it is what `EE51` tests to decide whether there is anything to
   * rub out. Two names for one offset is the §6.34 trap set deliberately, so there is one name and
   * the bits are named once, in `ShipFlags.h`, as `ShipStateBit`.
   */
  inline constexpr std::uint8_t SHIP_STATE_OFFSET = 31;

  /// The AI byte: bit 7 says the ship has AI, and the rest is aggression, or for a
  /// missile the slot it is locked on to (`KILLSHP` renumbers it). The bits are `AiBit`.
  inline constexpr std::uint8_t SHIP_AI_OFFSET = 32;

  /// The offsets NWSHP writes before the block is copied into its slot.
  inline constexpr std::uint8_t SHIP_HEAP_LOW_OFFSET = 33;  ///< INWK+33 / INWK+34, the ship's
  inline constexpr std::uint8_t SHIP_HEAP_HIGH_OFFSET = 34; ///< own heap pointer
  inline constexpr std::uint8_t SHIP_ENERGY_OFFSET = 35;    ///< INWK+35, from blueprint byte 14

  /// NEWB is at zero page 45 and INWK at 9, so NEWB IS INWK+36 -- the last byte of the block,
  /// and the reason NI% is thirty-seven rather than the thirty-six the workspace looks.
  inline constexpr std::uint8_t SHIP_FLAGS_OFFSET = 36;

  // The layout, said once so that a moved constant cannot move silently.
  static_assert(SHIP_NOSE_OFFSET == SHIP_Z_OFFSET + 3u, "the orientation follows the position");
  static_assert(SHIP_ROOF_OFFSET == SHIP_NOSE_OFFSET + 6u && SHIP_SIDE_OFFSET == SHIP_ROOF_OFFSET + 6u, "three vectors of six");
  static_assert(SHIP_SPEED_OFFSET == SHIP_SIDE_OFFSET + 6u, "the speed follows the last vector");
  static_assert(SHIP_STATE_OFFSET == 31u && SHIP_AI_OFFSET == 32u && SHIP_HEAP_LOW_OFFSET == 33u && SHIP_HEAP_HIGH_OFFSET == 34u &&
                  SHIP_ENERGY_OFFSET == 35u && SHIP_FLAGS_OFFSET == 36u && SHIP_BLOCK_SIZE == 37u,
                "the tail of the block");

  // INWK+9/10 and kin -- one component of an orientation vector is a `SignMag16`, which lives
  // in `EliteTypes.h` since M2-b because the arithmetic kernel takes and returns the same shape.

  /// INWK+9..14, +15..20 or +21..26 -- nosev, roofv or sidev: three components of two bytes.
  struct Vector16
  {
    SignMag16 x{};
    SignMag16 y{};
    SignMag16 z{};

    [[nodiscard]] constexpr bool operator==(const Vector16&) const noexcept = default;
  };

  /*
   * INWK, and one entry of K% -- a single ship.
   *
   * The fields are the bytes' meanings and in the bytes' order; `ToBytes` and `FromBytes` are the
   * one place that order is written down, and the `static_assert` under them is the round trip.
   * `state`, `ai` and `newb` are still bytes with `ShipFlags.h`'s named bits over them -- the
   * owning types are a later slice's; the heap pointer is a `HeapOffset` (M1-f). A whole ship copies as a struct (`NWSHP`'s `NWL3` loop, `MAL2`'s and `MAL3`'s), and
   * a routine that copies PART of one (`WSL2`'s thirty-two bytes, `MAL4`'s twenty-nine) says so
   * through the codec, where the count is visible.
   */
  struct Ship
  {
    SignMag24 x{};
    SignMag24 y{};
    SignMag24 z{};

    Vector16 nose{}; ///< Nosev
    Vector16 roof{}; ///< Roofv
    Vector16 side{}; ///< Sidev

    std::uint8_t speed = 0;
    std::uint8_t acceleration = 0;
    std::uint8_t rollCounter = 0;
    std::uint8_t pitchCounter = 0;
    std::uint8_t state = 0;        ///< See `SHIP_STATE_OFFSET` and `ShipStateBit`
    std::uint8_t ai = 0;           ///< See `AiBit`
    HeapOffset heap{};             ///< INWK+33 and 34 -- XX19, the ship's own line heap
    std::uint8_t energy = 0;
    std::uint8_t traits = 0;         ///< INWK+36, which is NEWB -- see `NewbBit`

    [[nodiscard]] constexpr bool operator==(const Ship&) const noexcept = default;

    // ---- the routines entered with an offset in a register ------------------------------------

    /// INWK,X with X = 0, 3 or 6 -- the axis a routine was entered with. No caller passes
    /// anything else, and the last case is the fall-through so that a wrong offset reads as z
    /// rather than as nothing; M2 replaces the offset with an enumeration and the question goes.
    [[nodiscard]] constexpr SignMag24& PositionAt(std::uint8_t _offset) noexcept
    {
      return (_offset == SHIP_X_OFFSET) ? x : (_offset == SHIP_Y_OFFSET) ? y : z;
    }
    [[nodiscard]] constexpr const SignMag24& PositionAt(std::uint8_t _offset) const noexcept
    {
      return (_offset == SHIP_X_OFFSET) ? x : (_offset == SHIP_Y_OFFSET) ? y : z;
    }

    /// INWK,Y with Y = 9, 15 or 21 -- the vector a routine was entered with.
    [[nodiscard]] constexpr Vector16& VectorAt(std::uint8_t _offset) noexcept
    {
      return (_offset == SHIP_NOSE_OFFSET) ? nose : (_offset == SHIP_ROOF_OFFSET) ? roof : side;
    }
    [[nodiscard]] constexpr const Vector16& VectorAt(std::uint8_t _offset) const noexcept
    {
      return (_offset == SHIP_NOSE_OFFSET) ? nose : (_offset == SHIP_ROOF_OFFSET) ? roof : side;
    }

    /// INWK,Y / INWK+1,Y -- one component of a vector, where a routine was entered with the
    /// component's own offset (`MVS5`'s X and Y, `MAS1`'s Y): 9, 11 or 13 in nosev, 15, 17 or 19
    /// in roofv, 21, 23 or 25 in sidev.
    [[nodiscard]] constexpr SignMag16& ComponentAt(std::uint8_t _offset) noexcept
    {
      Vector16& vector = VectorAt(static_cast<std::uint8_t>(_offset - ((_offset - SHIP_NOSE_OFFSET) % 6u)));
      const std::uint8_t component = static_cast<std::uint8_t>((_offset - SHIP_NOSE_OFFSET) % 6u);
      return (component == 0u) ? vector.x : (component == 2u) ? vector.y : vector.z;
    }
    [[nodiscard]] constexpr const SignMag16& ComponentAt(std::uint8_t _offset) const noexcept
    {
      const Vector16& vector = VectorAt(static_cast<std::uint8_t>(_offset - ((_offset - SHIP_NOSE_OFFSET) % 6u)));
      const std::uint8_t component = static_cast<std::uint8_t>((_offset - SHIP_NOSE_OFFSET) % 6u);
      return (component == 0u) ? vector.x : (component == 2u) ? vector.y : vector.z;
    }

    // ---- the wire format --------------------------------------------------------------------

    /// The K% layout -- the thirty-seven bytes `NWSHP` writes through `(INF),Y` and the
    /// oracle compares.
    [[nodiscard]] constexpr std::array<std::uint8_t, SHIP_BLOCK_SIZE> ToBytes() const noexcept
    {
      std::array<std::uint8_t, SHIP_BLOCK_SIZE> bytes{};
      const auto axis = [&bytes](std::uint8_t _at, const SignMag24& _axis) noexcept
      {
        bytes[_at] = _axis.lo;
        bytes[_at + 1u] = _axis.hi;
        bytes[_at + 2u] = _axis.sgn;
      };
      const auto vector = [&bytes](std::uint8_t _at, const Vector16& _vector) noexcept
      {
        bytes[_at] = _vector.x.lo;
        bytes[_at + 1u] = _vector.x.hi;
        bytes[_at + 2u] = _vector.y.lo;
        bytes[_at + 3u] = _vector.y.hi;
        bytes[_at + 4u] = _vector.z.lo;
        bytes[_at + 5u] = _vector.z.hi;
      };
      axis(SHIP_X_OFFSET, x);
      axis(SHIP_Y_OFFSET, y);
      axis(SHIP_Z_OFFSET, z);
      vector(SHIP_NOSE_OFFSET, nose);
      vector(SHIP_ROOF_OFFSET, roof);
      vector(SHIP_SIDE_OFFSET, side);
      bytes[SHIP_SPEED_OFFSET] = speed;
      bytes[SHIP_ACCELERATION_OFFSET] = acceleration;
      bytes[SHIP_ROLL_OFFSET] = rollCounter;
      bytes[SHIP_PITCH_OFFSET] = pitchCounter;
      bytes[SHIP_STATE_OFFSET] = state;
      bytes[SHIP_AI_OFFSET] = ai;
      bytes[SHIP_HEAP_LOW_OFFSET] = static_cast<std::uint8_t>(heap.Address() & 0xFFu);
      bytes[SHIP_HEAP_HIGH_OFFSET] = static_cast<std::uint8_t>(heap.Address() >> 8);
      bytes[SHIP_ENERGY_OFFSET] = energy;
      bytes[SHIP_FLAGS_OFFSET] = traits;
      return bytes;
    }

    /// The ship thirty-seven bytes of the K% layout describe -- the inverse of `ToBytes`, and how
    /// the bridge and the tests put bytes into a ship.
    [[nodiscard]] static constexpr Ship FromBytes(std::span<const std::uint8_t, SHIP_BLOCK_SIZE> _bytes) noexcept
    {
      Ship ship;
      const auto axis = [&_bytes](std::uint8_t _at) noexcept
      {
        return SignMag24{_bytes[_at], _bytes[_at + 1u], _bytes[_at + 2u]};
      };
      const auto vector = [&_bytes](std::uint8_t _at) noexcept
      {
        return Vector16{{_bytes[_at], _bytes[_at + 1u]}, {_bytes[_at + 2u], _bytes[_at + 3u]}, {_bytes[_at + 4u], _bytes[_at + 5u]}};
      };
      ship.x = axis(SHIP_X_OFFSET);
      ship.y = axis(SHIP_Y_OFFSET);
      ship.z = axis(SHIP_Z_OFFSET);
      ship.nose = vector(SHIP_NOSE_OFFSET);
      ship.roof = vector(SHIP_ROOF_OFFSET);
      ship.side = vector(SHIP_SIDE_OFFSET);
      ship.speed = _bytes[SHIP_SPEED_OFFSET];
      ship.acceleration = _bytes[SHIP_ACCELERATION_OFFSET];
      ship.rollCounter = _bytes[SHIP_ROLL_OFFSET];
      ship.pitchCounter = _bytes[SHIP_PITCH_OFFSET];
      ship.state = _bytes[SHIP_STATE_OFFSET];
      ship.ai = _bytes[SHIP_AI_OFFSET];
      ship.heap = HeapOffset::FromAddress(static_cast<std::uint16_t>(_bytes[SHIP_HEAP_LOW_OFFSET] | (_bytes[SHIP_HEAP_HIGH_OFFSET] << 8)));
      ship.energy = _bytes[SHIP_ENERGY_OFFSET];
      ship.traits = _bytes[SHIP_FLAGS_OFFSET];
      return ship;
    }

    /// `FromBytes` over an array, which is what every caller has.
    [[nodiscard]] static constexpr Ship FromBytes(const std::array<std::uint8_t, SHIP_BLOCK_SIZE>& _bytes) noexcept
    {
      return FromBytes(std::span<const std::uint8_t, SHIP_BLOCK_SIZE>{_bytes});
    }
  };

  namespace Detail
  {
    /// A ship with every byte distinct, so that the round trip below proves the order and not
    /// just the count.
    [[nodiscard]] constexpr std::array<std::uint8_t, SHIP_BLOCK_SIZE> DistinctShipBytes() noexcept
    {
      std::array<std::uint8_t, SHIP_BLOCK_SIZE> bytes{};
      for (std::size_t at = 0; at < bytes.size(); ++at)
      {
        bytes[at] = static_cast<std::uint8_t>(0x21u + at * 5u);
      }
      return bytes;
    }
  } // namespace Detail

  static_assert(Ship::FromBytes(Ship{}.ToBytes()) == Ship{}, "the codec round-trips the empty ship");
  static_assert(Ship::FromBytes(Detail::DistinctShipBytes()).ToBytes() == Detail::DistinctShipBytes(),
                "the codec round-trips thirty-seven distinct bytes in their order");

} // namespace Elite
