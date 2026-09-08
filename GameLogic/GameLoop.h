#pragma once

#include "Ports.h"
#include "Universe.h"

#include "Charts.h"
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
   * returning: parts 1 and 2 each end by creating a ship and carry straight on, and part 3 reaches
   * part 4 as the untaken side of a branch rather than by any transfer of its own. They are
   * numbered by the annotation, not by the code. Splitting them would mean inventing entry points
   * the original does not have, which is §6.122's lesson from `TACTICS` and `DOCKIT`.
   *
   * WHERE IT STARTS is not the top of part 2. `.TT100` opens by calling the flight loop and then
   * running the delay and message counters, all of which `Main.cpp` already runs (§6.128). The
   * spawning proper begins where the witchspace flag is read, and that is what this function is.
   */

  /*
   * IT RETURNS NOTHING, AND WORKING OUT WHY IS THE FIRST THING THIS SLICE GOT WRONG.
   *
   * Six paths through these four parts jump to part 5 and the seventh falls out of the pirate
   * loop, which looks like two outcomes a caller would have to tell apart -- and the first version
   * of this header said so, with an enum for it. It is one outcome. `MLOOP` is not the top of the
   * loop: it is the LABEL ON PART 5, three instructions of stack reset at the head of the laser
   * cooling. So the jump and the fall-through arrive at the same instruction, and the only thing
   * the jump adds is a reset of the 6502's stack pointer, throwing away return addresses the port
   * does not have.
   *
   * The whole loop is `TT100` (the flight loop, then this) into `MLOOP` (cool, redraw) into `FRCE`
   * (keys, then `TT102`), and `FRCE` chooses the next pass's entry from `QQ12`. A port that
   * believed the enum would have given `Main.cpp` a branch the original does not have.
   */

  /// What `TT100`'s head decided, which the original says by whether it jumps straight to part 5.
  enum class LoopHead : std::uint8_t
  {
    SkipSpawning, ///< 6502: .ytq -- 255 passes in 256 go straight to part 5
    Spawn,        ///< 6502: the branch that skips the exit -- `MCNT` reached zero
  };

  /*
   * 6502: TT100 -- the three instructions after the flight loop and before the spawning (slice 4c-d).
   *
   * `DLY` is a countdown that STOPS AT ZERO rather than wrapping: a value of 0 decrements to 255,
   * which is negative, and the routine puts it straight back. So the only pass that reaches `me2`
   * is the one where it was exactly 1.
   *
   * `me2` then does two different things by view. On a space view it re-sends the message token
   * through `MESS`, which ERASES the message because the printer is an EOR, and zeroes `DLY`; on a
   * text screen it calls `CLYNS` instead, because a message there is in the bottom rows and those
   * are cleared rather than un-printed.
   *
   * **AND THE SPAWNER RUNS ONE PASS IN 256.** `MCNT` comes down by one each pass and only a zero
   * lets the spawner run -- and it is a byte that nothing else resets, so it wraps, and everything
   * slice 4c-a built happens on the pass where it reaches zero. Reading parts 1 to 4 without this
   * makes the bubble fill 256 times too fast, which is the kind of wrong that looks like a working
   * game for the first few seconds.
   */
  [[nodiscard]] LoopHead RunLoopHead(Universe& _universe, Ports& _ports) noexcept;

  /*
   * 6502: MLOOP's first six instructions and `EE20` -- the two countdowns, before the `QQ11` gate.
   *
   * BOTH ARE COOLING. `GNTMP` is the laser temperature the LT dial reads and `LASCT` is the pulse
   * laser's own countdown, and the two together are why a gun works at all: part 3 of the flight
   * loop refuses to fire while `LASCT` is non-zero and jams the gun for good at a `GNTMP` of 242.
   * Both are written by firing and this is the only place either comes down, so without it a pulse
   * laser fires exactly once per flight and the LT bar only ever rises.
   *
   * `LASCT` FALLS BY TWO AND NOT BY ONE, and the second step is SKIPPED when the first reaches
   * zero, so an odd countdown stops at zero and an even one steps through it. A port that
   * subtracted two would go negative on the odd values.
   *
   * It is named and exported rather than left inside `RunLoopTail` because it is ABOVE part 5's
   * gate on `QQ11`: a DOCKED pass runs these two and nothing else in the routine, and the docked
   * loop in `Main.cpp` has no other way to reach them. `RunLoopTail` calls it too, so there is one
   * copy of the arithmetic and not two -- which is the whole of §6.34's trap, and the merge that
   * prompted this had a second transcription of it in the executable (§6.146).
   */
  void CoolTheGuns(FlightStatus& _status) noexcept;

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
   * The two instructions that reset the 6502's stack are not ported: they are there because six
   * paths reach here by `JMP` rather than by returning, and a port whose calls are calls has
   * nothing to reset.
   *
   * THE TRUMBLES ARE IN IT, and they belong to slice 4d. They are here anyway, because they are in
   * this routine and splitting it would mean inventing an entry point the original does not have
   * -- the same argument parts 1 to 4 settled. What 4d owns is the sprites and `MVTRIBS`; the
   * breeding arithmetic and the squeak are `MLOOP`'s.
   */
  /*
   * Returns the VERTICAL SYNCS the pass asks to wait for, which is 0 or 2.
   *
   * The original's delay is a hardware wait, and `check_gamelogic.py` forbids `GameLogic` a clock,
   * so the frames are counted back to the executable rather than slept through here. That is the
   * same decision ADR-005 §3 made for the loop as a whole: the game says how long, the platform
   * decides how to spend it.
   */
  [[nodiscard]] std::uint8_t RunLoopTail(Universe& _universe, Ports& _ports, Commander& _commander, std::uint8_t _authorNames,
                                         bool _carryIn) noexcept;

  /*
   * 6502: CYL2, COU and PACK -- the three ship types the spawner names that no earlier slice did.
   *
   * `PACK` is not a type of its own: the source says `PACK = SH3`, so the pack hunters are the
   * eight blueprints from the Sidewinder up, and part 4 picks one by masking a random byte to
   * three bits and adding that base WITH THE CARRY: nothing between the mask and the addition
   * clears it, so a live carry moves the pack one type along.
   */
  /// 6502: PACK = SH3 -- the pack hunters begin at the Sidewinder (`ShipType::Sidewinder`), and the
  /// Cougar is `ShipType::Cougar`; both live in the enumeration now.

  /// 6502: the wait at the end of a docked pass -- two vertical syncs with the author-names
  /// option off, which is the only frame cap anywhere in the main loop (§6.17).
  inline constexpr std::uint8_t LOOP_DELAY_FRAMES = 2;

  /// 6502: the roll a Trumble breeds on, which is a CARRY added to the low byte and not an
  /// increment: 36 values in 256, so the population grows about one pass in seven.
  inline constexpr std::uint8_t TRUMBLE_BREED_ROLL = 220;

  /// 6502: compared twice, and it does two things: above it the squeak comes half as often, and it
  /// is the sound of them burning rather than of them squeaking.
  inline constexpr std::uint8_t TRUMBLE_BURN_TEMPERATURE = 224;

  /// 6502: the z high byte every ship spawned by part 2 starts at, which is why traders and
  /// canisters always appear at the same distance in a random direction.
  inline constexpr std::uint8_t SPAWN_DISTANCE = 38;

  /// 6502: the roll part 2 opens with. Above it there is no spawn at all this pass.
  inline constexpr std::uint8_t TRADER_ROLL = 35;

  /// 6502: three pieces of junk in the bubble and part 2 stops trying.
  inline constexpr std::uint8_t JUNK_LIMIT = 3;

  /// 6502: above this a rock hermit rather than a canister or an alloy plate.
  inline constexpr std::uint8_t HERMIT_ROLL = 252;

  /// 6502: the Thargoid roll, and the same constant is compared again, against a rotated random
  /// byte, for the bounty hunter.
  inline constexpr std::uint8_t THARGOID_ROLL = 200;

  /// 6502: the government test, and the split between a lone hunter and a pack of pirates.
  inline constexpr std::uint8_t GOVERNMENT_ROLL = 90;
  inline constexpr std::uint8_t PIRATE_ROLL = 100;

  /// 6502: the one value of `Ze`'s byte that sends part 3 to `fothg`, which is a Thargoid or,
  /// once in the game, a Cougar.
  inline constexpr std::uint8_t COUGAR_BYTE = 136;

  /*
   * 6502: THERE -- are we in the Constrictor's system, and the answer is in the CARRY.
   *
   * Galaxy 2 at (144, 33), and the routine is four compares and an `RTS`. The shape worth keeping
   * is the branch that lands ON the `RTS`, one byte past the label, stepping OVER the `CLC`: a
   * match returns with the carry the last `CMP` set -- which is set, because the compare was
   * equal. Every other path runs the `CLC`. A port that returned `true`/`false` from the compares
   * would be right here and would have lost the idiom; this returns the flag, like
   * `SubtractShipAxis` does (§6.126).
   */
  [[nodiscard]] bool AtConstrictorSystem(const Commander& _commander) noexcept;

  /*
   * 6502: GTHG -- a Thargoid and its Thargon, which is the only pair the game spawns together.
   *
   * `Ze` puts the block at a random bearing, `INWK+32` gets &FF (hostile, fastest AI), and then two
   * `NWSHP`s. The second is a `JMP` rather than a `JSR`, so `GTHG` returns whatever the Thargon's
   * creation returned and the Thargoid's answer is discarded -- a full bubble gets the mothership
   * and no escort, and the port reproduces that rather than tidying it.
   */
  NewShip SpawnThargoidPair(Bubble& _bubble, Ship& _work, Rng& _rng, const Blueprint*& _blueprint, bool _carryIn) noexcept;

  /*
   * Where a spawning pass left the loop (Modernize.md M6-a-1).
   *
   * SIX PATHS JUMP TO PART 5 AND ONE DOES NOT, AND THE ONE IS PART 1's. `.MTT4` finishes by
   * creating its ship and falls through into the instruction after it, which is `.TT100` -- the
   * TOP of part 2, not the part-3 label further down the same file. So spawning a trader costs a
   * second flight frame and a second turn of both counters before the pass reaches `MLOOP`, and
   * parts 3 and 4 do not run at all on that pass: no police, no bounty hunters, no Thargoid.
   *
   * The port returned to part 3 there until M6-a-1, because the header above reads "parts 1 and 2
   * each end by creating a ship and carry straight on" and part 2's file holds part 3's label as
   * well. The addresses settle it -- `MTT4` is &84C3, `TT100` is &84ED and `MTT1` is &8562 -- and
   * the annotation says it in words: "add a new ship of type A to the local bubble and fall
   * through into the main game loop again".
   */
  enum class SpawnOutcome : std::uint8_t
  {
    Ended,     ///< 6502: the jump to `MLOOP` -- the pass carries on into part 5
    Restarted, ///< 6502: part 1 falling into `.TT100` -- another flight frame first
  };

  /*
   * Main game loop parts 1 to 4: everything that arrives in the bubble on its own.
   *
   * `_carryIn` is the flag the first `DORND` rotates in, which is whatever `Main.cpp` reached the
   * spawner with -- §6.121 is the reason it is a parameter rather than an assumption.
   */
  [[nodiscard]] SpawnOutcome RunSpawning(Universe& _universe, bool _carryIn) noexcept;

} // namespace Elite
