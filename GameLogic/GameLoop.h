#pragma once

#include "Commander.h"
#include "Dashboard.h"
#include "FlightLoop.h"
#include "Rng.h"
#include "ShipSlot.h"
#include "StartUp.h"

#include <cstdint>

namespace Elite
{

  /*
   * The main game loop's non-flight half -- parts 1 to 6 (slices 4c-a and 4c-d).
   *
   * The file was `Spawner` while it held only parts 1 to 4, and §6.121's rule caught up with it
   * when part 5 arrived: a name that records which routine asked first stops being true when a
   * second one asks. What is here is `MLOOP` and everything it falls through, which is the half of
   * the loop that is not `M%`.
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
   * 6502: MLOOP -- main game loop part 5, which is the loop's own housekeeping (slice 4c-d).
   *
   * SIXTY-FIVE INSTRUCTIONS, AND THE PORT HAD FOURTEEN OF THEM PLACED BY HAND. §6.128 read three
   * player reports -- a dead letter key on the buy screen, dials that never moved, a laser that
   * fired once -- diagnosed all three as this routine being absent, and recorded that all three
   * were fixed. Auditing it for this slice found that only the FIRST had been: `DrawDials` still
   * had one caller, the one-off fill on a screen change, and nothing anywhere cooled `GNTMP` or
   * counted `LASCT` down. Transcribing fragments of a routine into an executable is how two of
   * three went missing without anything going red, which is why the whole of it is here instead
   * (§6.138).
   *
   * `LDX #&FF / TXS` is not ported: it resets the 6502's stack because six paths reach here by
   * `JMP` rather than by returning, and a port whose calls are calls has nothing to reset.
   *
   * THE TRUMBLES ARE IN IT, and they belong to slice 4d. They are here anyway, because they are in
   * this routine and splitting it would mean inventing an entry point the original does not have
   * -- the same argument parts 1 to 4 settled. What 4d owns is the sprites and `MVTRIBS`; the
   * breeding arithmetic and the squeak are `MLOOP`'s.
   */
  /*
   * Returns the VERTICAL SYNCS the pass asks to wait for, which is 0 or 2.
   *
   * `JSR DELAY` is a hardware wait and `check_gamelogic.py` forbids `GameLogic` a clock, so the
   * frames are counted back to the executable rather than slept through here. That is the same
   * decision ADR-005 §3 made for the loop as a whole: the game says how long, the platform decides
   * how to spend it.
   */
  [[nodiscard]] std::uint8_t RunLoopTail(FlightLoop& _loop, CommanderBlock& _commander, std::uint8_t _authorNames, bool _carryIn) noexcept;

  /*
   * 6502: CYL2, COU and PACK -- the three ship types the spawner names that no earlier slice did.
   *
   * `PACK` is not a type of its own: the source says `PACK = SH3`, so the pack hunters are the
   * eight blueprints from the Sidewinder up, and part 4 picks one with `AND #7 / ADC #PACK`.
   */
  inline constexpr std::uint8_t SHIP_TYPE_COBRA_PIRATE = 24;                 ///< 6502: CYL2
  inline constexpr std::uint8_t SHIP_TYPE_COUGAR = 32;                       ///< 6502: COU
  inline constexpr std::uint8_t SHIP_TYPE_PACK_FIRST = SHIP_TYPE_SIDEWINDER; ///< 6502: PACK = SH3

  /// 6502: LDY #2 / JSR DELAY -- two vertical syncs on a docked screen with the author-names
  /// option off, which is the only frame cap anywhere in the main loop (§6.17).
  inline constexpr std::uint8_t LOOP_DELAY_FRAMES = 2;

  /// 6502: CMP #220 -- the roll a Trumble breeds on, which is a CARRY added to the low byte and
  /// not an increment: 36 values in 256, so the population grows about one pass in seven.
  inline constexpr std::uint8_t TRUMBLE_BREED_ROLL = 220;

  /// 6502: CMP #224 -- read twice, and it does two things: above it the squeak comes half as often
  /// and it is the sound of them burning rather than of them squeaking.
  inline constexpr std::uint8_t TRUMBLE_BURN_TEMPERATURE = 224;

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
