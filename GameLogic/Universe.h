#pragma once

#include "Tokens.h"

#include <array>
#include <cstdint>

namespace Elite
{

/*
 * The universe, which is not stored anywhere (slice 2a).
 *
 * Elite's eight galaxies of 256 systems are not a table. They are six bytes of seed and a
 * twisting rule: twist four times and you are at the next system, and everything about a system
 * -- its name, economy, government, technology, population, productivity and the words used to
 * describe it -- falls out of the seed you arrive at. That is how 2,048 systems fit in a machine
 * that could not have held a hundred.
 *
 * Which makes this the most seed-sensitive code in the game. One wrong carry and the port has a
 * different universe: not a crash, not a glitch, just a Lave that is somewhere else. The tests
 * for this slice compare every field of all 2,048 systems against the shipped routines, because
 * anything less would leave the wrong ones to be found by a player.
 */

/// 6502: QQ15 -- three sixteen-bit seeds, low byte first, as the game holds them.
struct SystemSeeds
{
  std::array<std::uint8_t, 6> bytes{};

  [[nodiscard]] bool operator==(const SystemSeeds&) const = default;
};

/*
 * The galaxy the game starts in.
 *
 * These six bytes live in the default commander block (`NA%`), not in a table of their own, so
 * they properly belong to slice 2d. They are here because 2a cannot generate a galaxy without
 * them, and a test compares them against the shipped commander so that the copy cannot drift
 * from the original while nobody is looking.
 */
inline constexpr SystemSeeds GALAXY_ONE_SEEDS = { { 0x4A, 0x5A, 0x48, 0x02, 0x53, 0xB7 } };

/*
 * 6502: QQ3, QQ4, QQ5, QQ6, QQ7 -- everything TT24 works out about a system from its seed.
 *
 * Population and productivity are the two the player sees as numbers; the other three are
 * indices into token tables, which is why they are bytes rather than enumerations for now. Names
 * come from the token printer instead, because the game builds them a letter-pair at a time.
 */
struct SystemData
{
  std::uint8_t economy = 0;    ///< 6502: QQ3, 0 to 7
  std::uint8_t government = 0; ///< 6502: QQ4, 0 to 7
  std::uint8_t techLevel = 0;  ///< 6502: QQ5
  std::uint8_t population = 0; ///< 6502: QQ6, in hundreds of millions
  std::uint16_t productivity = 0; ///< 6502: QQ7, sixteen bits

  [[nodiscard]] bool operator==(const SystemData&) const = default;
};

/*
 * 6502: TT54 -- twist the seeds once.
 *
 * Three sixteen-bit values, each becoming the next: s0 takes s1, s1 takes s2, and s2 becomes the
 * sum of the old s0 and s1 plus the new s1. The order the original writes them in matters --
 * s2's new value is computed from bytes that have already been overwritten -- so the port keeps
 * that order rather than the one that reads more naturally.
 */
void TwistSeeds(SystemSeeds& _seeds) noexcept;

/*
 * 6502: TT20 -- twist four times, which is one system along.
 *
 * The original spells this as two nested fall-throughs into TT54 rather than a loop, which is
 * four calls for the price of two JSRs. There is nothing else to it.
 */
void NextSystem(SystemSeeds& _seeds) noexcept;

/*
 * 6502: Ghy's G1 loop -- the galactic hyperdrive.
 *
 * Each of the six bytes is rotated left by one bit, INDEPENDENTLY: `ASL A` takes the byte's top
 * bit into the carry and `ROL` puts it back into the same byte's bottom. Not a rotate of the
 * three sixteen-bit seeds, which is what it looks like and would give a different universe.
 */
void NextGalaxy(SystemSeeds& _seeds) noexcept;

/*
 * 6502: TT24 -- everything about a system except its name and its description.
 *
 * Almost every line of this is an ADC whose carry comes from the line before, including two that
 * take it from an `LSR` and an `ASL` rather than from an addition. The port models the flag
 * rather than the arithmetic, because the arithmetic alone is off by one in several places.
 */
[[nodiscard]] SystemData GenerateSystemData(const SystemSeeds& _seeds) noexcept;

/*
 * 6502: cpl -- print a system's name.
 *
 * Three or four letter-pairs, and which it is depends on bit 6 of the first seed byte. The seeds
 * are twisted between pairs and put back afterwards, so this leaves them as it found them --
 * callers depend on that, because printing a name must not move the universe.
 */
void PrintSystemName(TokenPrinter& _printer, SystemSeeds& _seeds) noexcept;

} // namespace Elite
