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
 * 6502: TT111's result -- the system nearest the crosshairs, and how far away it is.
 *
 * The routine leaves all of this behind in zero page and in QQ8/QQ9/QQ10, and its callers read
 * every part of it, so the port hands it back as one thing rather than through five outputs.
 */
struct NearestSystem
{
  SystemSeeds seeds;              ///< 6502: QQ15, left at the system that was found
  std::uint8_t index = 0;         ///< 6502: ZZ, its number within the galaxy
  std::uint8_t x = 0;             ///< 6502: QQ9, the found system's galactic x
  std::uint8_t y = 0;             ///< 6502: QQ10, and its y
  std::uint16_t distance = 0;     ///< 6502: QQ8, in tenths of a light year
  SystemData data;                ///< from the TT24 this routine falls into
};

/*
 * 6502: TT111 -- find the system nearest the crosshairs, and how far it is from where you are.
 *
 * Two halves that look alike and are not. The SEARCH walks all 256 systems of the galaxy and
 * measures with half of |dx| plus half of |dy| -- cheap, and good enough to pick a system. The
 * DISTANCE it then reports is the real one: dx squared plus half-dy squared, square-rooted, times
 * four. Using the search metric for the answer would put every system in the game at the wrong
 * range, and the two are four instructions apart in the original.
 *
 * The halving of dy in both is the chart's aspect ratio, not an approximation.
 *
 * Like several routines before it this one has no RTS: it ends in JMP TT24, so a caller gets the
 * system's data as well, and the port returns that too rather than pretending the jump is not
 * there.
 */
[[nodiscard]] NearestSystem FindNearestSystem(const SystemSeeds& _galaxy, std::uint8_t _crosshairX,
                                              std::uint8_t _crosshairY, std::uint8_t _currentX,
                                              std::uint8_t _currentY) noexcept;

/*
 * 6502: cpl -- print a system's name.
 *
 * Three or four letter-pairs, and which it is depends on bit 6 of the first seed byte. The seeds
 * are twisted between pairs and put back afterwards, so this leaves them as it found them --
 * callers depend on that, because printing a name must not move the universe.
 */
void PrintSystemName(TokenPrinter& _printer, SystemSeeds& _seeds) noexcept;

} // namespace Elite
