#pragma once

#include <cstdint>

/*
 * The ship types (Design/Modernize.md slice M1-b).
 *
 * Its own header, and a small one, because both `ShipBlueprint.h` (which indexes `XX21` by type)
 * and `ShipSlot.h` (which includes it) need the enumeration, and the second includes the first.
 * Nothing here allocates, throws, or reaches the platform.
 */
namespace Elite
{

  /*
   * The ship types, in the order `XX21` lists their blueprints (Modernize.md M1-b).
   *
   * A type is a number the game uses three ways: as an index into `XX21` and `MANY`, as the value
   * of a slot in `FRIN`, and as `TYPE` while a ship is being moved -- where 128 and 129 are the
   * planet and the sun, which have no blueprint and whose bit 7 is what `BMI` tests. The enumerators
   * carry the original's values, `Byte` and `TypeOf` cross to and from the byte the tables and the
   * image hold, and the predicates below are the source's own ranges: `JL` to `JH` is the junk
   * (`ESC` to `SHU+2`, exclusive), `PLT` to `SPL` the wreckage `SFS1` tumbles, `PACK` the eight from
   * the Sidewinder up. The names the port used before -- `SST` for the station, `HER` for the
   * hermit, `CYL2` for the pirate Cobra -- are in the comments beside the enumerators.
   *
   * Type 33, the Dodo, is a BLUEPRINT and never a ship in a slot: `NWSPS` writes its address into
   * the station's entry of `XX21` and the station stays type 2. It is here because the table is.
   */
  enum class ShipType : std::uint8_t
  {
    None = 0,            ///< An empty slot in FRIN
    Missile = 1,         ///< The only type that carries a target slot in its AI byte
    Station = 2,         ///< The Coriolis, or the Dodo where `NWSPS` says so; skips the heap
    EscapePod = 3,       ///< ESC, and JL, the bottom of the junk range
    AlloyPlate = 4,      ///< PLT, the first of the wreckage
    Canister = 5,
    Boulder = 6,
    Asteroid = 7,
    Splinter = 8,        ///< SPL, the last of the wreckage
    Shuttle = 9,
    Transporter = 10,    ///< SHU+1, which the source never names and `TA1` counts
    CobraMk3 = 11,       ///< CYL, and JH -- the first type that is not junk
    Python = 12,
    Boa = 13,
    Anaconda = 14,
    RockHermit = 15,     ///< Counts as junk despite its number
    Viper = 16,
    Sidewinder = 17,     ///< SH3, and PACK -- the first of the eight pack hunters
    Mamba = 18,
    Krait = 19,
    Adder = 20,          ///< The title screen's ship
    Gecko = 21,
    CobraMk1 = 22,
    Worm = 23,
    CobraMk3Pirate = 24, ///< A different blueprint and a different bounty
    AspMk2 = 25,
    PythonPirate = 26,
    FerDeLance = 27,
    Moray = 28,
    Thargoid = 29,       ///< Exempt from the energy bomb
    Thargon = 30,
    Constrictor = 31,    ///< The mission ship; a laser is halved from here up unless military
    Cougar = 32,
    Dodo = 33,           ///< A blueprint the station borrows, never a slot's type
    Planet = 128,        ///< TYPE with bit 7 set -- no blueprint, moved by MV40
    Sun = 129,           ///< %10000001, which a mask-and-compare against &81 singles out
  };

  /// The byte a type is in `FRIN`, `MANY`'s index, `TYPE` and the image.
  [[nodiscard]] constexpr std::uint8_t Byte(ShipType _type) noexcept
  {
    return static_cast<std::uint8_t>(_type);
  }

  /// The type a byte names. Every byte is a value of the enumeration (it has a fixed underlying
  /// type), so a sweep over all 256 can go through here and a routine can still test bit 7.
  [[nodiscard]] constexpr ShipType TypeOf(std::uint8_t _byte) noexcept
  {
    return static_cast<ShipType>(_byte);
  }

  /// TYPE tested for its SIGN -- the planet or the sun, which have no blueprint.
  [[nodiscard]] constexpr bool IsBody(ShipType _type) noexcept
  {
    return (Byte(_type) & 0x80u) != 0u;
  }

  /// JL to JH, exclusive, and HER -- what `NWSHP` and `KILLSHP` count in `JUNK`.
  [[nodiscard]] constexpr bool IsJunk(ShipType _type) noexcept
  {
    return _type == ShipType::RockHermit || (Byte(_type) >= Byte(ShipType::EscapePod) && Byte(_type) < Byte(ShipType::CobraMk3));
  }

  /// The cargo range, plate to splinter, bounded at both ends -- which `SFS1` gives a
  /// random tumble and nothing else does.
  [[nodiscard]] constexpr bool IsWreckage(ShipType _type) noexcept
  {
    return Byte(_type) >= Byte(ShipType::AlloyPlate) && Byte(_type) <= Byte(ShipType::Splinter);
  }

} // namespace Elite
