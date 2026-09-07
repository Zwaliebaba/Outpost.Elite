#pragma once

#include "Galaxy.h"

#include <array>
#include <cstdint>
#include <span>

namespace Elite
{

  /*
   * The commander (slice 2d).
   *
   * Everything the game remembers about you is seventy-seven consecutive bytes, and the save file
   * IS those bytes with an eight-byte name in front. Until Modernize.md's M1-d that was a byte
   * array with named offsets, on the argument that a struct needs a serialiser and a serialiser
   * can drift from the layout. It is a struct now, and the serialiser is `ToBytes`/`FromBytes`
   * below, written once in terms of the `Field` offsets and proved by `CommanderTests`' round
   * trips against the assembled original -- the acceptance criterion is still that a commander
   * file extracted from an original disk loads, and it still does.
   *
   * Two checksums guard the block, and they are the game's copy protection rather than error
   * detection: CHECK threads a carry through seventy-three additions and CHECK2 folds a rotate in
   * as well, and DFAULT spins in an infinite loop when the first does not match. That loop is the
   * one behaviour here the port does not reproduce -- see LoadCommander.
   */

  /// 6502: TP to CHK -- the block SVE writes, `LDX #&4C` and count down.
  inline constexpr std::size_t COMMANDER_BLOCK_SIZE = 77;

  /// 6502: NAME -- eight bytes, the last of which is the carriage return that ends it.
  inline constexpr std::size_t COMMANDER_NAME_SIZE = 8;

  /// A saved commander: the name, then the block. Eighty-five bytes on disk.
  inline constexpr std::size_t COMMANDER_FILE_SIZE = COMMANDER_NAME_SIZE + COMMANDER_BLOCK_SIZE;

  /*
   * Where each field sits in the block -- THE WIRE FORMAT, which `ToBytes` and `FromBytes` are
   * written in and the tests address the oracle's `TP` through.
   *
   * These are the label addresses minus TP's, taken from the assembled build rather than counted
   * from the source, because several fields are followed by bytes no label names and counting
   * would silently close the gaps.
   */
  enum class Field : std::size_t
  {
    MissionProgress = 0, ///< 6502: TP
    SystemX = 1,         ///< 6502: QQ0 -- where you are, in galactic coordinates
    SystemY = 2,         ///< 6502: QQ1
    GalaxySeeds = 3,     ///< 6502: QQ21 -- six bytes, and the whole galaxy follows from them
    Cash = 9,            ///< 6502: CASH -- four bytes, most significant first, in tenths
    Fuel = 13,           ///< 6502: QQ14 -- in light years times ten
    Competition = 14,    ///< 6502: COK -- the flags the competition code was built from
    GalaxyNumber = 15,   ///< 6502: GCNT
    Lasers = 16,         ///< 6502: LASER -- six bytes: front, rear, left, right and two unused
    /*
     * 6502: CRGO -- and it is TWO GREATER than the capacity it describes.
     *
     * A standard hold is 22 here and holds 20 tonnes; a large one is 37 and holds 35. The comment
     * in the original says why: it makes the arithmetic in `tnpr`, which decides whether a
     * purchase fits, "slightly more efficient". So a port that read this as the capacity would let
     * the player carry two tonnes too many, and the default commander's block says 22.
     */
    CargoCapacity = 22,
    CargoHold = 23,        ///< 6502: QQ20 -- seventeen goods
    Ecm = 40,              ///< 6502: ECM
    FuelScoops = 41,       ///< 6502: BST
    EnergyBomb = 42,       ///< 6502: BOMB
    EnergyUnit = 43,       ///< 6502: ENGY
    DockingComputer = 44,  ///< 6502: DKCMP
    GalacticDrive = 45,    ///< 6502: GHYP
    EscapePod = 46,        ///< 6502: ESCP, with one byte after it that nothing names
    Tribbles = 48,         ///< 6502: TRIBBLE -- two bytes
    KillsLow = 50,         ///< 6502: TALLYL
    Missiles = 51,         ///< 6502: NOMSL
    LegalStatus = 52,      ///< 6502: FIST
    Availability = 53,     ///< 6502: AVL -- seventeen goods, the market's stock
    MarketRandomiser = 70, ///< 6502: QQ26
    Kills = 71,            ///< 6502: TALLY -- two bytes
    SaveCount = 73,        ///< 6502: SVC
    Checksum2Byte = 74,    ///< 6502: CHK2
    Checksum3Byte = 75,    ///< 6502: CHK3
    ChecksumByte = 76,     ///< 6502: CHK
  };

  /*
   * 6502: CASH -- four bytes, MOST significant first, in tenths of a credit.
   *
   * The opposite way round from everything else in the game, which keeps its sixteen-bit values
   * low byte first. A port that used one convention throughout would give the player either
   * fourteen pence or several million credits. It is a type of its own so that the one place the
   * order is written down is `Bytes`, and so that a routine that reads byte 2 (`EN6`'s Trumbles
   * offer, the competition number) says which byte it means.
   */
  struct Credits
  {
    std::uint32_t tenths = 0;

    /// 6502: CASH+n -- one of the four bytes, most significant first.
    [[nodiscard]] constexpr std::uint8_t Byte(std::size_t _index) const noexcept
    {
      return static_cast<std::uint8_t>(tenths >> (8u * (3u - _index)));
    }
    constexpr void SetByte(std::size_t _index, std::uint8_t _value) noexcept
    {
      const std::uint32_t shift = 8u * (3u - _index);
      tenths = (tenths & ~(std::uint32_t{0xFFu} << shift)) | (static_cast<std::uint32_t>(_value) << shift);
    }

    [[nodiscard]] constexpr bool operator==(const Credits&) const noexcept = default;
  };

  /*
   * 6502: TALLY and TRIBBLE -- a sixteen-bit count kept as two bytes, low byte first, which the
   * game steps and shifts a byte at a time (`INC TALLY+1`, the Trumbles' `ROR TRIBBLE+1 / ROR
   * TRIBBLE`). The halves are fields because that is how every routine reaches them; `Value` is
   * for the ones that read the pair (`TT111`'s rank, the market's count).
   */
  struct Tally
  {
    std::uint8_t lo = 0;
    std::uint8_t hi = 0;

    [[nodiscard]] constexpr std::uint16_t Value() const noexcept
    {
      return static_cast<std::uint16_t>(lo | (hi << 8));
    }

    [[nodiscard]] constexpr bool operator==(const Tally&) const noexcept = default;
  };

  /*
   * 6502: QQ14 -- fuel, in tenths of a light year (M5-a-10).
   *
   * ADR-006 §2 parked this type at M1 "for M5, where a type earns its operators", and these are
   * the operators it earned: the three arithmetic rules the game applies to the byte, each of
   * which was spelled out at its one site with a private copy of the number seventy. Everything
   * else that reads the byte -- the dial, the fuel circle, the printer, the codec -- reads
   * `tenths`, because for those it IS a byte.
   */
  struct FuelBurn;

  struct LightYearsTenths
  {
    std::uint8_t tenths = 0;

    /// 6502: MA23's scooping -- `LSR A / ADC QQ14 / CMP #70 / BCC P%+4 / LDA #70`: the amount, plus
    /// the bit the LSR shifted out as the carry into the add, saturating at a full tank.
    [[nodiscard]] constexpr LightYearsTenths Scooped(std::uint8_t _amount, bool _carry) const noexcept
    {
      const std::uint8_t sum = static_cast<std::uint8_t>(_amount + tenths + (_carry ? 1u : 0u));
      return {(sum < FULL_TANK_TENTHS) ? sum : FULL_TANK_TENTHS};
    }

    /// 6502: the jump's `SEC / SBC QQ8 / BCS P%+4 / LDA #0` -- a jump costing more than the tank
    /// holds leaves it EMPTY rather than wrapped, and the carry says which it was. Defined below
    /// `FuelBurn`, which it returns.
    [[nodiscard]] constexpr FuelBurn Burned(std::uint8_t _tenths) const noexcept;

    /// 6502: TT111's `LDA QQ8+1 / BNE TT147 / LDA QQ14 / CMP QQ8 / BCC TT147` -- a distance is in
    /// range when its high byte is clear and the tank holds at least its low byte.
    [[nodiscard]] constexpr bool Reaches(std::uint16_t _distanceTenths) const noexcept
    {
      return (_distanceTenths >> 8) == 0u && tenths >= static_cast<std::uint8_t>(_distanceTenths & 0xFFu);
    }

    [[nodiscard]] constexpr bool operator==(const LightYearsTenths&) const noexcept = default;

    /// 6502: the 70 that NA%, nosurviv, MA23 and the equipment screen all write.
    static constexpr std::uint8_t FULL_TANK_TENTHS = 70;
  };

  /// A full tank: seven light years. One definition, where until M5-a-10 `FlightLoop`, `Flight.h`
  /// and `Equipment.cpp` each kept their own seventy.
  inline constexpr LightYearsTenths FULL_TANK{LightYearsTenths::FULL_TANK_TENTHS};

  /// What a jump leaves: the tank, and the `SBC`'s carry, which the jump's tunnel roll rotates in.
  struct FuelBurn
  {
    LightYearsTenths left;
    bool carry; ///< 6502: set when the tank held the distance -- the flag `SBC` leaves
  };

  constexpr FuelBurn LightYearsTenths::Burned(std::uint8_t _tenths) const noexcept
  {
    const bool held = tenths >= _tenths;
    return {{held ? static_cast<std::uint8_t>(tenths - _tenths) : std::uint8_t{0}}, held};
  }

  /*
   * 6502: TP to CHK -- the commander, as the fields the seventy-seven bytes are.
   *
   * In the bytes' order, with the two bytes no label names kept as fields so that the codec is a
   * plain walk: `lasers` has six entries because `LASER` is six bytes of which four are mounts,
   * and `spare` is the byte after `ESCP`. The equipment is one byte each with the original's
   * `0`/`&FF` (and the energy bomb's `&7F`) values -- typing them is a later slice's. The three
   * checksums are fields because the block on disk carries them and `LoadCommander` leaves the
   * last one untouched.
   */
  struct Commander
  {
    std::uint8_t missionProgress = 0;         ///< 6502: TP
    std::uint8_t systemX = 0;                 ///< 6502: QQ0
    std::uint8_t systemY = 0;                 ///< 6502: QQ1
    SystemSeeds galaxySeeds{};                ///< 6502: QQ21 -- six bytes
    Credits cash{};                           ///< 6502: CASH -- four bytes, most significant first
    LightYearsTenths fuel;                    ///< 6502: QQ14 -- light years times ten
    std::uint8_t competition = 0;             ///< 6502: COK
    std::uint8_t galaxyNumber = 0;            ///< 6502: GCNT
    std::array<std::uint8_t, 6> lasers{};     ///< 6502: LASER -- front, rear, left, right, and two nothing names
    std::uint8_t cargoCapacity = 0;           ///< 6502: CRGO -- two more than the hold holds
    std::array<std::uint8_t, 17> cargoHold{}; ///< 6502: QQ20 -- seventeen goods
    std::uint8_t ecm = 0;                     ///< 6502: ECM
    std::uint8_t fuelScoops = 0;              ///< 6502: BST
    std::uint8_t energyBomb = 0;              ///< 6502: BOMB
    std::uint8_t energyUnit = 0;              ///< 6502: ENGY
    std::uint8_t dockingComputer = 0;         ///< 6502: DKCMP
    std::uint8_t galacticDrive = 0;           ///< 6502: GHYP
    std::uint8_t escapePod = 0;               ///< 6502: ESCP
    std::uint8_t spare = 0;                   ///< 6502: the byte after ESCP that nothing names
    Tally tribbles{};                         ///< 6502: TRIBBLE -- two bytes
    std::uint8_t killsFraction = 0;           ///< 6502: TALLYL
    std::uint8_t missiles = 0;                ///< 6502: NOMSL
    std::uint8_t legalStatus = 0;             ///< 6502: FIST
    std::array<std::uint8_t, 17> availability{}; ///< 6502: AVL -- the market's stock
    std::uint8_t marketRandomiser = 0;        ///< 6502: QQ26
    Tally kills{};                            ///< 6502: TALLY -- two bytes
    std::uint8_t saveCount = 0;               ///< 6502: SVC
    std::uint8_t checksum2 = 0;               ///< 6502: CHK2
    std::uint8_t checksum3 = 0;               ///< 6502: CHK3
    std::uint8_t checksum = 0;                ///< 6502: CHK

    [[nodiscard]] constexpr bool operator==(const Commander&) const noexcept = default;

    /*
     * 6502: QQ20,X for X in 0 to 21 -- the hold, and then the five fittings that follow it in the
     * block: ECM, BST, BOMB, ENGY, DKCMP.
     *
     * `OUCH` picks a slot under 22 and empties `QQ20,X`, which is a cargo type below seventeen and
     * a piece of equipment from seventeen on -- the same indexed load reaching past the array's end
     * into the bytes laid out after it. While the commander was seventy-seven bytes that was one
     * subscript; with the hold a typed array it is this, because `cargoHold[17]` is not a fitting
     * but a bounds assertion in Debug and whatever sits there in Release (plan §6.158). The caller
     * has already tested `< 22`; a slot at or past it lands on the docking computer rather than on
     * memory this struct does not model.
     */
    [[nodiscard]] constexpr std::uint8_t& HoldOrFitting(std::uint8_t _slot) noexcept
    {
      switch (_slot)
      {
      case 17u:
        return ecm;
      case 18u:
        return fuelScoops;
      case 19u:
        return energyBomb;
      case 20u:
        return energyUnit;
      default:
        return (_slot < cargoHold.size()) ? cargoHold[_slot] : dockingComputer;
      }
    }

    /// 6502: the TP layout -- the seventy-seven bytes SVE writes and DFAULT reads.
    [[nodiscard]] constexpr std::array<std::uint8_t, COMMANDER_BLOCK_SIZE> ToBytes() const noexcept
    {
      std::array<std::uint8_t, COMMANDER_BLOCK_SIZE> bytes{};
      const auto at = [&bytes](Field _field) noexcept -> std::uint8_t& { return bytes[static_cast<std::size_t>(_field)]; };
      const auto run = [&bytes](Field _field, const auto& _values) noexcept
      {
        for (std::size_t index = 0; index < _values.size(); ++index)
        {
          bytes[static_cast<std::size_t>(_field) + index] = _values[index];
        }
      };
      at(Field::MissionProgress) = missionProgress;
      at(Field::SystemX) = systemX;
      at(Field::SystemY) = systemY;
      run(Field::GalaxySeeds, galaxySeeds.bytes);
      for (std::size_t index = 0; index < 4u; ++index)
      {
        bytes[static_cast<std::size_t>(Field::Cash) + index] = cash.Byte(index);
      }
      at(Field::Fuel) = fuel.tenths;
      at(Field::Competition) = competition;
      at(Field::GalaxyNumber) = galaxyNumber;
      run(Field::Lasers, lasers);
      at(Field::CargoCapacity) = cargoCapacity;
      run(Field::CargoHold, cargoHold);
      at(Field::Ecm) = ecm;
      at(Field::FuelScoops) = fuelScoops;
      at(Field::EnergyBomb) = energyBomb;
      at(Field::EnergyUnit) = energyUnit;
      at(Field::DockingComputer) = dockingComputer;
      at(Field::GalacticDrive) = galacticDrive;
      at(Field::EscapePod) = escapePod;
      bytes[static_cast<std::size_t>(Field::EscapePod) + 1u] = spare;
      at(Field::Tribbles) = tribbles.lo;
      bytes[static_cast<std::size_t>(Field::Tribbles) + 1u] = tribbles.hi;
      at(Field::KillsLow) = killsFraction;
      at(Field::Missiles) = missiles;
      at(Field::LegalStatus) = legalStatus;
      run(Field::Availability, availability);
      at(Field::MarketRandomiser) = marketRandomiser;
      at(Field::Kills) = kills.lo;
      bytes[static_cast<std::size_t>(Field::Kills) + 1u] = kills.hi;
      at(Field::SaveCount) = saveCount;
      at(Field::Checksum2Byte) = checksum2;
      at(Field::Checksum3Byte) = checksum3;
      at(Field::ChecksumByte) = checksum;
      return bytes;
    }

    /// The commander seventy-seven bytes of the TP layout describe -- the inverse of `ToBytes`.
    [[nodiscard]] static constexpr Commander FromBytes(std::span<const std::uint8_t, COMMANDER_BLOCK_SIZE> _bytes) noexcept
    {
      Commander commander;
      const auto at = [&_bytes](Field _field) noexcept { return _bytes[static_cast<std::size_t>(_field)]; };
      const auto run = [&_bytes](Field _field, auto& _values) noexcept
      {
        for (std::size_t index = 0; index < _values.size(); ++index)
        {
          _values[index] = _bytes[static_cast<std::size_t>(_field) + index];
        }
      };
      commander.missionProgress = at(Field::MissionProgress);
      commander.systemX = at(Field::SystemX);
      commander.systemY = at(Field::SystemY);
      run(Field::GalaxySeeds, commander.galaxySeeds.bytes);
      for (std::size_t index = 0; index < 4u; ++index)
      {
        commander.cash.SetByte(index, _bytes[static_cast<std::size_t>(Field::Cash) + index]);
      }
      commander.fuel.tenths = at(Field::Fuel);
      commander.competition = at(Field::Competition);
      commander.galaxyNumber = at(Field::GalaxyNumber);
      run(Field::Lasers, commander.lasers);
      commander.cargoCapacity = at(Field::CargoCapacity);
      run(Field::CargoHold, commander.cargoHold);
      commander.ecm = at(Field::Ecm);
      commander.fuelScoops = at(Field::FuelScoops);
      commander.energyBomb = at(Field::EnergyBomb);
      commander.energyUnit = at(Field::EnergyUnit);
      commander.dockingComputer = at(Field::DockingComputer);
      commander.galacticDrive = at(Field::GalacticDrive);
      commander.escapePod = at(Field::EscapePod);
      commander.spare = _bytes[static_cast<std::size_t>(Field::EscapePod) + 1u];
      commander.tribbles.lo = at(Field::Tribbles);
      commander.tribbles.hi = _bytes[static_cast<std::size_t>(Field::Tribbles) + 1u];
      commander.killsFraction = at(Field::KillsLow);
      commander.missiles = at(Field::Missiles);
      commander.legalStatus = at(Field::LegalStatus);
      run(Field::Availability, commander.availability);
      commander.marketRandomiser = at(Field::MarketRandomiser);
      commander.kills.lo = at(Field::Kills);
      commander.kills.hi = _bytes[static_cast<std::size_t>(Field::Kills) + 1u];
      commander.saveCount = at(Field::SaveCount);
      commander.checksum2 = at(Field::Checksum2Byte);
      commander.checksum3 = at(Field::Checksum3Byte);
      commander.checksum = at(Field::ChecksumByte);
      return commander;
    }

    /// `FromBytes` over an array, which is what every caller has.
    [[nodiscard]] static constexpr Commander FromBytes(const std::array<std::uint8_t, COMMANDER_BLOCK_SIZE>& _bytes) noexcept
    {
      return FromBytes(std::span<const std::uint8_t, COMMANDER_BLOCK_SIZE>{_bytes});
    }
  };

  namespace Detail
  {
    /// Seventy-seven distinct bytes, so that the round trip below proves the order of the codec.
    [[nodiscard]] constexpr std::array<std::uint8_t, COMMANDER_BLOCK_SIZE> DistinctCommanderBytes() noexcept
    {
      std::array<std::uint8_t, COMMANDER_BLOCK_SIZE> bytes{};
      for (std::size_t at = 0; at < bytes.size(); ++at)
      {
        bytes[at] = static_cast<std::uint8_t>(0x13u + at * 3u);
      }
      return bytes;
    }
  } // namespace Detail

  static_assert(Commander::FromBytes(Commander{}.ToBytes()) == Commander{}, "the codec round-trips the empty commander");
  static_assert(Commander::FromBytes(Detail::DistinctCommanderBytes()).ToBytes() == Detail::DistinctCommanderBytes(),
                "the codec round-trips seventy-seven distinct bytes in their order");

  /*
   * 6502: CHECK -- the checksum SVE writes to CHK and DFAULT insists on.
   *
   * Seventy-three steps, and every one of them is `ADC` with no `CLC`, so a carry out of one
   * addition is carried into the next. It also reads the block one byte BEFORE the index it EORs
   * with, so each step mixes two neighbouring bytes rather than one. Neither is decoration: get
   * the carry wrong and the checksum agrees for a great many blocks and not for the one the player
   * saved.
   *
   * The accumulator starts at 73, which is the loop counter, not a constant anyone chose.
   */
  [[nodiscard]] std::uint8_t Checksum(const Commander& _block) noexcept;

  /*
   * 6502: CHECK2 -- the second checksum, which goes into CHK3.
   *
   * The same shape with two more operations per step: the index is EORed in, and then the
   * accumulator is ROTATED through the carry the last addition left before the next addition
   * consumes what the rotate shifted out. So the carry is read, written, and read again inside one
   * step, and there is no way to write this as arithmetic.
   */
  [[nodiscard]] std::uint8_t Checksum2(const Commander& _block) noexcept;

  /*
   * 6502: NA2% -- the commander the game hands a new player.
   *
   * Lave at (20, 173), a hundred credits, seven light years of fuel, twenty tonnes of cargo space
   * and a pulse laser. The seeds in it are the ones slice 2a carries as GALAXY_ONE_SEEDS, and a
   * test checks the two still agree.
   */
  [[nodiscard]] Commander DefaultCommander() noexcept;

  /// 6502: NA2% -- the eight bytes of name that go in front of the block.
  [[nodiscard]] std::array<std::uint8_t, COMMANDER_NAME_SIZE> DefaultCommanderName() noexcept;

  /*
   * 6502: SVE's SVL1 loop, plus the two CHECK calls -- write a commander out.
   *
   * The checksums go into the FILE and not into the live commander: `STA CHK3`, `STA CHK` and
   * `STA CHK2` all write to NA%, which is the copy about to be written to disk, and the block at TP
   * is left exactly as it was. So saving does not change the commander, and a port that stored them
   * back would give the next save a different checksum from the one the original computes.
   *
   * ALL THREE, and the third one was missing here until 2026-09-03. `CHK2` is the checksum EOR &A9
   * and SV1 writes it four instructions past the competition number, long after the two CHECK
   * calls -- so a reading that stopped at those looked complete. `LoadCommander` only reads it, to
   * decide whether to flag the file as tampered, so a round trip agreed with itself and the gap
   * survived 221 compared blocks. Building the save flow on top is what found it.
   */
  void SaveCommander(const Commander& _block, std::span<const std::uint8_t, COMMANDER_NAME_SIZE> _name,
                     std::span<std::uint8_t, COMMANDER_FILE_SIZE> _outFile) noexcept;

  /*
   * 6502: DFAULT's QUL1 loop and the check that follows it -- read a commander in.
   *
   * BOTH checksums are checked, and the branch for either failure goes BACKWARDS to the first one:
   * `BNE doitagain`. So a tampered save file hangs the game on a black screen rather than being
   * reported. That is copy protection rather than a bug, and it is the one behaviour in this slice
   * the port refuses to reproduce -- a hang is not something a caller can handle, and ADR-003's
   * "match before improving" is about what the game COMPUTES.
   *
   * Three things happen between the two checks that are easy to miss. The competition flags in the
   * block are updated: bit 6 is always set, and bit 7 is set when the first checksum EORed with
   * &A9 does not equal the second stored one -- which is how a tampered file is remembered rather
   * than rejected. Both checksums are computed over the FILE, so that update does not feed into
   * the second one. And the copy loop stops one byte early, so the block's own checksum byte is
   * never loaded and whatever the caller had there survives.
   */
  [[nodiscard]] bool LoadCommander(std::span<const std::uint8_t, COMMANDER_FILE_SIZE> _file, Commander& _outBlock,
                                   std::span<std::uint8_t, COMMANDER_NAME_SIZE> _outName) noexcept;

} // namespace Elite
