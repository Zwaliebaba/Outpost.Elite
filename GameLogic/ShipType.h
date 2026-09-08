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
   * 6502: the ship types, in the order `XX21` lists their blueprints (Modernize.md M1-b).
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
    None = 0,            ///< 6502: an empty slot in FRIN
    Missile = 1,         ///< 6502: MSL -- the only type that carries a target slot in its AI byte
    Station = 2,         ///< 6502: SST -- the Coriolis, or the Dodo where `NWSPS` says so; skips the heap
    EscapePod = 3,       ///< 6502: ESC, and JL, the bottom of the junk range
    AlloyPlate = 4,      ///< 6502: PLT, the first of the wreckage
    Canister = 5,        ///< 6502: OIL
    Boulder = 6,
    Asteroid = 7,        ///< 6502: AST
    Splinter = 8,        ///< 6502: SPL, the last of the wreckage
    Shuttle = 9,         ///< 6502: SHU
    Transporter = 10,    ///< 6502: SHU+1, which the source never names and `TA1` counts
    CobraMk3 = 11,       ///< 6502: CYL, and JH -- the first type that is not junk
    Python = 12,
    Boa = 13,
    Anaconda = 14,       ///< 6502: ANA
    RockHermit = 15,     ///< 6502: HER -- counts as junk despite its number
    Viper = 16,          ///< 6502: COPS
    Sidewinder = 17,     ///< 6502: SH3, and PACK -- the first of the eight pack hunters
    Mamba = 18,
    Krait = 19,          ///< 6502: KRA
    Adder = 20,          ///< 6502: ADA -- the title screen's ship
    Gecko = 21,
    CobraMk1 = 22,
    Worm = 23,           ///< 6502: WRM
    CobraMk3Pirate = 24, ///< 6502: CYL2 -- a different blueprint and a different bounty
    AspMk2 = 25,
    PythonPirate = 26,
    FerDeLance = 27,
    Moray = 28,
    Thargoid = 29,       ///< 6502: THG -- exempt from the energy bomb
    Thargon = 30,        ///< 6502: TGL
    Constrictor = 31,    ///< 6502: CON -- the mission ship; a laser is halved from here up unless military
    Cougar = 32,         ///< 6502: COU
    Dodo = 33,           ///< 6502: DOD -- a blueprint the station borrows, never a slot's type
    Planet = 128,        ///< 6502: TYPE with bit 7 set -- no blueprint, moved by MV40
    Sun = 129,           ///< 6502: %10000001, which a mask-and-compare against &81 singles out
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

  /// 6502: TYPE tested for its SIGN -- the planet or the sun, which have no blueprint.
  [[nodiscard]] constexpr bool IsBody(ShipType _type) noexcept
  {
    return (Byte(_type) & 0x80u) != 0u;
  }

  /// 6502: JL to JH, exclusive, and HER -- what `NWSHP` and `KILLSHP` count in `JUNK`.
  [[nodiscard]] constexpr bool IsJunk(ShipType _type) noexcept
  {
    return _type == ShipType::RockHermit || (Byte(_type) >= Byte(ShipType::EscapePod) && Byte(_type) < Byte(ShipType::CobraMk3));
  }

  /// 6502: the cargo range, plate to splinter, bounded at both ends -- which `SFS1` gives a
  /// random tumble and nothing else does.
  [[nodiscard]] constexpr bool IsWreckage(ShipType _type) noexcept
  {
    return Byte(_type) >= Byte(ShipType::AlloyPlate) && Byte(_type) <= Byte(ShipType::Splinter);
  }

} // namespace Elite
