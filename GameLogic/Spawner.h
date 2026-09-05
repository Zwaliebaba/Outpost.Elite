#pragma once

#include "Commander.h"
#include "Dashboard.h"
#include "Rng.h"
#include "ShipSlot.h"
#include "StartUp.h"

#include <cstdint>

namespace Elite
{

  /*
   * The main game loop's spawning rules -- parts 1 to 4 of six, and `GTHG` (slice 4c-a).
   *
   * This is what makes the universe a universe. `TACTICS` (slice 4a-c) decides what a ship already
   * in the bubble does; nothing before this slice put one there over time, so every ship the AI has
   * ever steered was created by a test. Four parts of the loop, run once per pass after the flight
   * loop returns, decide whether a trader, an asteroid, a canister, a policeman, a bounty hunter, a
   * Thargoid or a pack of pirates arrives -- and the whole thing is driven by `DORND`, so the
   * comparison against the shipped routine is exact rather than statistical (§6.134).
   *
   * THE FOUR PARTS ARE ONE ROUTINE. Every one of them falls through into the next rather than
   * returning: part 1 ends `JSR NWSHP` and continues into part 2, part 2 ends `JSR NWSHP` and
   * continues into part 3, and part 3's `BNE MLOOPS` is a conditional whose fall-through is part 4.
   * They are numbered by the annotation, not by the code. Splitting them would mean inventing
   * entry points the original does not have, which is §6.122's lesson from `TACTICS` and `DOCKIT`.
   *
   * WHERE IT STARTS is not the top of part 2. `.TT100` opens with `JSR M%` -- the flight loop --
   * and then the delay and message counters, all of which `Main.cpp` already runs (§6.128). The
   * spawning proper begins at `LDA MJ`, and that is what this function is.
   */

  /*
   * IT RETURNS NOTHING, AND WORKING OUT WHY IS THE FIRST THING THIS SLICE GOT WRONG.
   *
   * Six paths through these four parts end `JMP MLOOP` and the seventh falls out of the pirate
   * loop, which looks like two outcomes a caller would have to tell apart -- and the first version
   * of this header said so, with an enum for it. It is one outcome. `MLOOP` is not the top of the
   * loop: it is the LABEL ON PART 5, three instructions of stack reset at the head of the laser
   * cooling. So `JMP MLOOP` and the fall-through arrive at the same instruction, and the only
   * thing the jump adds is `LDX #&FF / TXS` throwing away return addresses the port does not have.
   *
   * The whole loop is `TT100` (the flight loop, then this) into `MLOOP` (cool, redraw) into `FRCE`
   * (keys, then `TT102`), and `FRCE` chooses the next pass's entry from `QQ12`. A port that
   * believed the enum would have given `Main.cpp` a branch the original does not have.
   */

  /*
   * 6502: CYL2, COU and PACK -- the three ship types the spawner names that no earlier slice did.
   *
   * `PACK` is not a type of its own: the source says `PACK = SH3`, so the pack hunters are the
   * eight blueprints from the Sidewinder up, and part 4 picks one with `AND #7 / ADC #PACK`.
   */
  inline constexpr std::uint8_t SHIP_TYPE_COBRA_PIRATE = 24;                 ///< 6502: CYL2
  inline constexpr std::uint8_t SHIP_TYPE_COUGAR = 32;                       ///< 6502: COU
  inline constexpr std::uint8_t SHIP_TYPE_PACK_FIRST = SHIP_TYPE_SIDEWINDER; ///< 6502: PACK = SH3

  /// 6502: LDA #38 / STA INWK+7 -- the z high byte every ship spawned by part 2 starts at, which
  /// is why traders and canisters always appear at the same distance in a random direction.
  inline constexpr std::uint8_t SPAWN_DISTANCE = 38;

  /// 6502: CMP #35 -- the roll part 2 opens with. Above it there is no spawn at all this pass.
  inline constexpr std::uint8_t TRADER_ROLL = 35;

  /// 6502: CMP #3 -- three pieces of junk in the bubble and part 2 stops trying.
  inline constexpr std::uint8_t JUNK_LIMIT = 3;

  /// 6502: CMP #252 -- above this a rock hermit rather than a canister or an alloy plate.
  inline constexpr std::uint8_t HERMIT_ROLL = 252;

  /// 6502: CMP #200 -- the Thargoid roll, and the same constant gates the bounty hunter's `ROL A`.
  inline constexpr std::uint8_t THARGOID_ROLL = 200;

  /// 6502: CMP #90 and CMP #100 -- the government test, and the split between a lone hunter and a
  /// pack of pirates.
  inline constexpr std::uint8_t GOVERNMENT_ROLL = 90;
  inline constexpr std::uint8_t PIRATE_ROLL = 100;

  /// 6502: CMP #136 -- the one value of `Ze`'s byte that sends part 3 to `fothg`, which is a
  /// Thargoid or, once in the game, a Cougar.
  inline constexpr std::uint8_t COUGAR_BYTE = 136;

  /*
   * 6502: THERE -- are we in the Constrictor's system, and the answer is in the CARRY.
   *
   * Galaxy 2 at (144, 33), and the routine is four compares and an `RTS`. The shape worth keeping
   * is `BEQ THEX+1`: it lands on the `RTS` and steps OVER the `CLC`, so a match returns with the
   * carry the last `CMP` set -- which is set, because the compare was equal. Every other path runs
   * the `CLC`. A port that returned `true`/`false` from the compares would be right here and would
   * have lost the idiom; this returns the flag, like `SubtractShipAxis` does (§6.126).
   */
  [[nodiscard]] bool AtConstrictorSystem(const CommanderBlock& _commander) noexcept;

  /*
   * 6502: GTHG -- a Thargoid and its Thargon, which is the only pair the game spawns together.
   *
   * `Ze` puts the block at a random bearing, `INWK+32` gets &FF (hostile, fastest AI), and then two
   * `NWSHP`s. The second is a `JMP` rather than a `JSR`, so `GTHG` returns whatever the Thargon's
   * creation returned and the Thargoid's answer is discarded -- a full bubble gets the mothership
   * and no escort, and the port reproduces that rather than tidying it.
   */
  NewShip SpawnThargoidPair(Bubble& _bubble, ShipBlock& _work, Rng& _rng, std::uint16_t& _blueprint, bool _carryIn) noexcept;

  /*
   * Main game loop parts 1 to 4: everything that arrives in the bubble on its own.
   *
   * `_carryIn` is the flag the first `DORND` rotates in, which is whatever `Main.cpp` reached the
   * spawner with -- §6.121 is the reason it is a parameter rather than an assumption.
   */
  void RunSpawning(Bubble& _bubble, ShipBlock& _work, Rng& _rng, CommanderBlock& _commander, const CurrentSystem& _current,
                   const FlightStatus& _status, std::uint8_t& _explosionCount, std::uint16_t& _blueprint, bool _carryIn) noexcept;

} // namespace Elite
