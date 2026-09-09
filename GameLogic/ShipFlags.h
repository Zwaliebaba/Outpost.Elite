#pragma once

#include <concepts>
#include <cstdint>

/*
 * The three flag bytes of a ship, bit by bit (Design/Modernize.md slice M1-b).
 *
 * `INWK+31`, `INWK+32` and `NEWB` are each one byte that several routines read a bit of, and until
 * this slice each routine named the bit it wanted in its own file -- `SHIP_STATE_KILLED` in one,
 * `SHIP_KILLED` in another, `0x80u` in a third -- so the same bit had three spellings and one of
 * the names was wrong (§8). The bits are named here once, with the original's values, and the
 * helpers below are the `AND` and `ORA` a routine did against them -- as VALUES, so that a site
 * keeps the original's load-modify-store shape (`work.state = With(work.state, ...)` is
   * a load, an OR and a store back) and no helper takes a byte by reference. Storage stays
 * a byte: `Ship::State()` is still `std::uint8_t&`, the image still holds the byte, and the
 * oracle still compares it. A flags type that OWNS the byte, with `Set` and `Clear` as members,
 * is M1-c's, with the struct.
 *
 * Nothing here allocates, throws, or reaches the platform.
 */
namespace Elite
{

  /*
   * The ship's state, and it holds two things.
   *
   * The bottom three bits are how many missiles the ship still carries (`NWSHP` ORs them in from
   * blueprint byte 19, `TACTICS` decrements them with `DEC INWK+31`), and the top five are the
   * flags. Bit 6 means two things told apart by bit 5: `LL9` clears bits 6 and 7 in the instruction
   * that sets bit 5, so a ship stops firing as it starts to blow up, and `DOEXP` then uses the
   * vacated bit to mean "there is a cloud on the screen from last frame". Both names are here so
   * that a reader meets the right one.
   */
  enum class ShipStateBit : std::uint8_t
  {
    OnScreen = 0x08,   ///< Bit 3 -- drawn on the screen now, so the next `LL9` must rub it out
    OnScanner = 0x10,  ///< Bit 4 -- a blip on the scanner, which `SCAN` erases the same way
    Exploding = 0x20,  ///< Bit 5
    Firing = 0x40,     ///< Bit 6 while bit 5 is clear -- the laser line `MA8` draws
    CloudDrawn = 0x40, ///< Bit 6 while bit 5 is set -- `DOEXP` drew a cloud last frame
    Killed = 0x80,     ///< Bit 7 -- killed, and exploding once `LL9` has seen it
  };

  /// The bottom three bits of `INWK+31` -- the missiles left, which share the byte with the
  /// flags. Also how `NWSHP` reads blueprint byte 19, whose bottom three bits are laid out the same
  /// way.
  [[nodiscard]] constexpr std::uint8_t MissilesOf(std::uint8_t _state) noexcept
  {
    return static_cast<std::uint8_t>(_state & 0x07u);
  }

  /*
   * The AI byte, which reads differently for a missile and for anything else.
   *
   * For a ship it is `%A aaaaaa E`: bit 7 says `TACTICS` runs for it, bits 1 to 6 are its
   * aggression, bit 0 says it has an ECM. For a missile it is `%A tttttt 0`, the middle six the
   * slot of its target -- and a target of `%1xxxxx` is US, which is why `TACTICS` reads bit 6 with
   * its SIGN after a shift. So bit 6 has a name for each reading, and the two field helpers are the
   * shift pair the source uses to pack and unpack the target: shift up and force the top bit, mask
   * the top bit off and shift down.
   * unpack the target.
   */
  enum class AiBit : std::uint8_t
  {
    HasEcm = 0x01,        ///< Bit 0 of a ship's byte -- fitted with an ECM
    Hostile = 0x40,       ///< Bit 6 of a ship's byte -- the top of the aggression field
    AimedAtPlayer = 0x40, ///< Bit 6 of a missile's byte -- its target is us
    Active = 0x80,        ///< Bit 7 -- `TACTICS` runs for this ship
  };

  /// The top bit masked off and the rest shifted down -- the slot a missile is locked on to.
  [[nodiscard]] constexpr std::uint8_t MissileTargetOf(std::uint8_t _ai) noexcept
  {
    return static_cast<std::uint8_t>((_ai & 0x7Fu) >> 1u);
  }

  /// The slot shifted up with the top bit forced on -- the AI byte that locks a missile on.
  [[nodiscard]] constexpr std::uint8_t MissileAiFor(std::uint8_t _targetSlot) noexcept
  {
    return static_cast<std::uint8_t>((_targetSlot << 1u) | 0x80u);
  }

  /*
   * The ship's nature, one bit each, in the order the source's `TACTICS` walks them
   * one shift at a time.
   *
   * Bit 5 is the source's "innocent bystander", and it is what the STATION takes offence at: hit a
   * ship with it set and `ANGRY` angers the station too. Bit 6 is the cop bit, and it is the one
   * `KS1` masks out and folds into the legal status when a ship dies -- the port
   * had that bit named `NEWB_INNOCENT`, which is the wrong bit's name (§8).
   */
  enum class TraitBit : std::uint8_t
  {
    Trader = 0x01,       ///< Bit 0 -- flees at a roll of fifty or more
    BountyHunter = 0x02, ///< Bit 1 -- turns on you once `FIST` passes forty
    Hostile = 0x04,      ///< Bit 2 -- what `ANGRY` sets
    Pirate = 0x08,       ///< Bit 3 -- keeps clear of the station
    Docking = 0x10,      ///< Bit 4 -- heading for the station
    Innocent = 0x20,     ///< Bit 5 -- an innocent bystander, on the station's side
    Cop = 0x40,          ///< Bit 6 -- shooting one is a crime
    Remove = 0x80,       ///< Bit 7 -- scooped or docked, so leave the bubble
  };

  /// The three flag enumerations, and only those: the helpers below work on a byte with any of
  /// them, and refuse a bit from one byte against another.
  template <typename Bit>
  concept ShipFlagBit = std::same_as<Bit, ShipStateBit> || std::same_as<Bit, AiBit> || std::same_as<Bit, TraitBit>;

  /// The mask one or more bits of the same byte make -- the immediate of the `AND`, `ORA` or `EOR`.
  template <ShipFlagBit Bit, std::same_as<Bit>... More>
  [[nodiscard]] constexpr std::uint8_t Mask(Bit _bit, More... _more) noexcept
  {
    return static_cast<std::uint8_t>((static_cast<std::uint8_t>(_bit) | ... | static_cast<std::uint8_t>(_more)));
  }

  /// A mask and a branch -- the bit is set.
  template <ShipFlagBit Bit>
  [[nodiscard]] constexpr bool Has(std::uint8_t _bits, Bit _bit) noexcept
  {
    return (_bits & Mask(_bit)) != 0u;
  }

  /// A mask and a branch -- at least one of the bits is set.
  template <ShipFlagBit Bit, std::same_as<Bit>... More>
  [[nodiscard]] constexpr bool HasAny(std::uint8_t _bits, Bit _bit, More... _more) noexcept
  {
    return (_bits & Mask(_bit, _more...)) != 0u;
  }

  /// The byte ORed with the bits, as a value.
  template <ShipFlagBit Bit, std::same_as<Bit>... More>
  [[nodiscard]] constexpr std::uint8_t With(std::uint8_t _bits, Bit _bit, More... _more) noexcept
  {
    return static_cast<std::uint8_t>(_bits | Mask(_bit, _more...));
  }

  /// The byte masked against their complement, as a value.
  template <ShipFlagBit Bit, std::same_as<Bit>... More>
  [[nodiscard]] constexpr std::uint8_t Without(std::uint8_t _bits, Bit _bit, More... _more) noexcept
  {
    return static_cast<std::uint8_t>(_bits & static_cast<std::uint8_t>(~Mask(_bit, _more...)));
  }

} // namespace Elite
