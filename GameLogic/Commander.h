#pragma once

#include "Universe.h"

#include <array>
#include <cstdint>
#include <span>

namespace Elite
{

/*
 * The commander (slice 2d).
 *
 * Everything the game remembers about you is seventy-seven consecutive bytes, and the save file
 * IS those bytes with an eight-byte name in front. That is why this is a byte array with named
 * offsets rather than a struct of fields: a struct would need a serialiser, a serialiser can
 * drift from the layout, and the acceptance criterion for this slice is that a commander file
 * extracted from an original disk loads. Make the bytes the storage and there is nothing to
 * drift.
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
 * Where each field sits in the block.
 *
 * These are the label addresses minus TP's, taken from the assembled build rather than counted
 * from the source, because several fields are followed by bytes no label names and counting
 * would silently close the gaps.
 */
enum class Field : std::size_t
{
  MissionProgress = 0,  ///< 6502: TP
  SystemX = 1,          ///< 6502: QQ0 -- where you are, in galactic coordinates
  SystemY = 2,          ///< 6502: QQ1
  GalaxySeeds = 3,      ///< 6502: QQ21 -- six bytes, and the whole galaxy follows from them
  Cash = 9,             ///< 6502: CASH -- four bytes, most significant first, in tenths
  Fuel = 13,            ///< 6502: QQ14 -- in light years times ten
  Competition = 14,     ///< 6502: COK -- the flags the competition code was built from
  GalaxyNumber = 15,    ///< 6502: GCNT
  Lasers = 16,          ///< 6502: LASER -- six bytes: front, rear, left, right and two unused
  /*
   * 6502: CRGO -- and it is TWO GREATER than the capacity it describes.
   *
   * A standard hold is 22 here and holds 20 tonnes; a large one is 37 and holds 35. The comment
   * in the original says why: it makes the arithmetic in `tnpr`, which decides whether a
   * purchase fits, "slightly more efficient". So a port that read this as the capacity would let
   * the player carry two tonnes too many, and the default commander's block says 22.
   */
  CargoCapacity = 22,
  CargoHold = 23,       ///< 6502: QQ20 -- seventeen goods
  Ecm = 40,             ///< 6502: ECM
  FuelScoops = 41,      ///< 6502: BST
  EnergyBomb = 42,      ///< 6502: BOMB
  EnergyUnit = 43,      ///< 6502: ENGY
  DockingComputer = 44, ///< 6502: DKCMP
  GalacticDrive = 45,   ///< 6502: GHYP
  EscapePod = 46,       ///< 6502: ESCP, with one byte after it that nothing names
  Tribbles = 48,        ///< 6502: TRIBBLE -- two bytes
  KillsLow = 50,        ///< 6502: TALLYL
  Missiles = 51,        ///< 6502: NOMSL
  LegalStatus = 52,     ///< 6502: FIST
  Availability = 53,    ///< 6502: AVL -- seventeen goods, the market's stock
  MarketRandomiser = 70,///< 6502: QQ26
  Kills = 71,           ///< 6502: TALLY -- two bytes
  SaveCount = 73,       ///< 6502: SVC
  Checksum2Byte = 74,   ///< 6502: CHK2
  Checksum3Byte = 75,   ///< 6502: CHK3
  ChecksumByte = 76,    ///< 6502: CHK
};

/*
 * The commander's data block.
 *
 * Held as the bytes the game holds, so that a file written by the original loads and a file this
 * writes is one the original would accept. The accessors below are for the fields wider than a
 * byte, because those are where a port gets the endianness wrong.
 */
struct CommanderBlock
{
  std::array<std::uint8_t, COMMANDER_BLOCK_SIZE> bytes{};

  [[nodiscard]] std::uint8_t& At(Field _field) noexcept { return bytes[static_cast<std::size_t>(_field)]; }
  [[nodiscard]] std::uint8_t At(Field _field) const noexcept { return bytes[static_cast<std::size_t>(_field)]; }

  /*
   * 6502: CASH -- four bytes, MOST significant first.
   *
   * The opposite way round from everything else in the game, which keeps its sixteen-bit values
   * low byte first. A port that used one convention throughout would give the player either
   * fourteen pence or several million credits.
   */
  [[nodiscard]] std::uint32_t Cash() const noexcept;
  void SetCash(std::uint32_t _tenths) noexcept;

  /// 6502: TALLY -- two bytes, and this pair IS low byte first, unlike the cash above it.
  [[nodiscard]] std::uint16_t Kills() const noexcept;

  /// 6502: QQ21 -- the six seed bytes the whole galaxy is generated from.
  [[nodiscard]] SystemSeeds GalaxySeeds() const noexcept;
  void SetGalaxySeeds(const SystemSeeds& _seeds) noexcept;

  [[nodiscard]] bool operator==(const CommanderBlock&) const = default;
};

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
[[nodiscard]] std::uint8_t Checksum(const CommanderBlock& _block) noexcept;

/*
 * 6502: CHECK2 -- the second checksum, which goes into CHK3.
 *
 * The same shape with two more operations per step: the index is EORed in, and then the
 * accumulator is ROTATED through the carry the last addition left before the next addition
 * consumes what the rotate shifted out. So the carry is read, written, and read again inside one
 * step, and there is no way to write this as arithmetic.
 */
[[nodiscard]] std::uint8_t Checksum2(const CommanderBlock& _block) noexcept;

/*
 * 6502: NA2% -- the commander the game hands a new player.
 *
 * Lave at (20, 173), a hundred credits, seven light years of fuel, twenty tonnes of cargo space
 * and a pulse laser. The seeds in it are the ones slice 2a carries as GALAXY_ONE_SEEDS, and a
 * test checks the two still agree.
 */
[[nodiscard]] CommanderBlock DefaultCommander() noexcept;

/// 6502: NA2% -- the eight bytes of name that go in front of the block.
[[nodiscard]] std::array<std::uint8_t, COMMANDER_NAME_SIZE> DefaultCommanderName() noexcept;

/*
 * 6502: SVE's SVL1 loop, plus the two CHECK calls -- write a commander out.
 *
 * The checksums go into the FILE and not into the live commander: `STA CHK3` and `STA CHK` write
 * to NA%, which is the copy about to be written to disk, and the block at TP is left exactly as
 * it was. So saving does not change the commander, and a port that stored them back would give
 * the next save a different checksum from the one the original computes.
 */
void SaveCommander(const CommanderBlock& _block, std::span<const std::uint8_t, COMMANDER_NAME_SIZE> _name,
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
[[nodiscard]] bool LoadCommander(std::span<const std::uint8_t, COMMANDER_FILE_SIZE> _file, CommanderBlock& _outBlock,
                                 std::span<std::uint8_t, COMMANDER_NAME_SIZE> _outName) noexcept;

} // namespace Elite
