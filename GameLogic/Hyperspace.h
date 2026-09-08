#pragma once

#include "Ports.h"
#include "Universe.h"

#include "Charts.h"
#include "FlightLoop.h"
#include "Market.h"
#include "StartUp.h"
#include "Galaxy.h"

#include <cstdint>

namespace Elite
{

  /*
   * The jump, witchspace, and the galactic hyperdrive (slice 4c-b).
   *
   * `hyp` decided WHETHER to jump in slice 2d and left `JumpOutcome::Galactic` for something that
   * did not exist yet; `Main.cpp` has listed hyperspace among the actions it refuses by name since
   * phase 3. This is what answers both. Four routines, and `TT18` is the one that runs the others.
   *
   * THE FALL-THROUGHS ARE THE STRUCTURE, again. `hyp1` ends `STA gov` and continues into `GVL`, so
   * arriving somewhere generates that system's market as part of the same routine; `Ghy` ends
   * by printing a message and continues into `jmp`; and `TT18` ends by stepping the view and
   * continues into `TT110`, which is the launch. None of the three is a call.
   */

  /*
   * 6502: hyp1 -- arrive at the selected system, and fall into `GVL` to stock its market.
   *
   * `_findNearest` is the difference between the two entry points the game uses. `hyp1` opens
   * by calling `TT111`, which puts the system nearest the crosshairs into `QQ15`; `TT18` jumps to
   * `hyp1+3`, three bytes past it, because the chart has already chosen and calling it again would
   * pick the same system a second time for nothing. Two entries into one routine, distinguished by
   * a flag rather than by a second function, because there is only one routine.
   *
   * `EV` IS ZEROED here -- the encounter counter reset, so arriving somewhere new means the
   * spawner (slice 4c-a) starts its rate limit again.
   */
  /*
   * `_described` IS `QQ3` TO `QQ5`, AND IT IS NOT THE SYSTEM `QQ2` GETS. That is the finding this
   * routine hides. The six seed bytes are copied from `safehouse` -- what the countdown saved --
   * while the economy, tech level and government come from `QQ3`, `QQ5` and `QQ4`, which are
   * whatever the LAST `TT111` left behind. On the `hyp1` path that is the system nearest the
   * crosshairs, computed one instruction earlier; on the `hyp1+3` path it is whatever the chart
   * last looked at. They agree in a game played through `hyp`, and they are still two different
   * sources: a port that derived the cache from the seeds it just copied matched the oracle on
   * `QQ2` and disagreed on `QQ28` for the first crosshair position tried.
   */
  /*
   * `_explosionCount` WAS A `std::uint8_t&` UNTIL M5-a-3 (P10). It is `EV`, the encounter counter,
   * and it is `Universe::explosions` at every call site -- so the universe comes in and the
   * reference goes, along with the commander, the current system and the generator, which are the
   * same three fields at every site too. What stays by value is what varies by caller.
   */
  void ArriveAtSystem(Universe& _universe, SystemSeeds& _selected, const SystemSeeds& _target, SystemData& _described,
                      MarketState& _market, std::uint8_t _crosshairX, std::uint8_t _crosshairY, const SystemSeeds& _galaxy,
                      bool _findNearest) noexcept;

  /*
   * 6502: MJP -- witchspace, which is a jump that did not arrive.
   *
   * `TT66` clears to the space view, `LL164` draws the tunnel, `RES2` resets the bubble, and then
   * `MJ` is set from Y -- the flag that stops the spawner and refuses the fuel scoop.
   * **Y is whatever `RES2` left**, not a value this routine chose -- so the port passes the byte
   * rather than assuming a 1, and the oracle is what says which it is.
   *
   * `MJP1` spawns Thargoid pairs until there are more than three of them, and the THREE it
   * compares against is then stored into `NOSTM`: witchspace has three specks of dust instead of
   * the usual eighteen, and the constant is shared between the two on purpose.
   */
  void EnterWitchspace(Universe& _universe, Ports& _ports, Commander& _commander) noexcept;

  /*
   * 6502: ptg -- `COK` shifted right, the carry set, and `COK` rotated back left, and then it
   * FALLS INTO `MJP`.
   *
   * That is an OR WITH 1 and not a rotate: the shift and the rotate cancel, the set carry forces
   * bit 0, and bit 7 survives because the shift moved it down and the rotate moved it back. The
   * same shape as the shift-left/set/rotate-right that §6.126 found mis-ported twice, one bit the
   * other way round.
   *
   * `COK` is the competition flags byte, so holding the configuration key through a jump is
   * recorded in the commander file for ever.
   */
  void EnterWitchspaceCheating(Universe& _universe, Ports& _ports, Commander& _commander) noexcept;

  /*
   * What `TT18` did, which the original says by WHERE IT ENDS UP -- and that is four places.
   *
   * Three of them look alike from inside the routine and are not. The branch to `RTS111` returns
   * having drawn nothing; the branch to `TT114` JUMPS OUT to redraw the chart, which is a
   * different screen's job and not a return at all; and the fall-through past `INC QQ11` is the
   * launch. The port had the first two as one outcome and the oracle disagreed about the generator
   * on the short-range chart, because the original had gone off to draw it.
   */
  enum class JumpResult : std::uint8_t
  {
    Arrived,     ///< 6502: the fall-through into `TT110` -- the caller launches
    Witchspace,  ///< 6502: the branch to `MJP` -- three bytes in 256 miss the system
    NoRedraw,    ///< 6502: the branch to `RTS111` -- the view's low six bits are set, nothing drawn
    RedrawChart, ///< 6502: the branch to `TT114` -- a chart is up, and the caller redraws it
  };

  /*
   * 6502: TT18 -- spend the fuel and go, and it is the whole jump.
   *
   * The fuel minus the distance, and the carry branch steps over a two-byte load of zero, so a
   * jump that costs more than you have leaves you with none rather than with a wrapped byte. The
   * check that it is affordable happened in `hyp` (slice 2d); this is the arithmetic.
   *
   * The keyboard read masked with `PATG` is the cheat: holding the key with the configuration
   * option on forces witchspace. Then a random byte of 253 or more -- THREE in 256, not one --
   * does it anyway.
   */
  [[nodiscard]] JumpResult PerformJump(Universe& _universe, Ports& _ports, SystemSeeds& _selected, JumpState& _jump,
                                       SystemData& _described, MarketState& _market, 
                                       std::uint8_t _crosshairX, std::uint8_t _crosshairY, const SystemSeeds& _galaxy, bool _controlHeld,
                                       bool _authorNames) noexcept;

  /*
   * 6502: Ghy -- the galactic hyperdrive, which moves you a galaxy on and forgets your crimes.
   *
   * The branch taken when no drive is fitted is the one to read twice: it goes to `zZ+1`. `zZ` is a
   * TWO-BYTE instruction that loads 96 into the accumulator, so `zZ+1` addresses its operand -- and
   * 96 is &60, which is also the opcode for `RTS`. With no drive fitted the branch jumps into the
   * middle of an instruction and executes its argument as a return. §6.121's rule about idioms that
   * look like something else, in its purest form: there is no code at `zZ+1`.
   *
   * X STEPPED FROM 255 TO 0 AND STORED TWICE is why the drive is single-use and why it cleans your
   * record: the same zero goes into `GHYP` and into `FIST`.
   *
   * `G1` rotates each of the galaxy's six seed bytes left by one -- the shift in the accumulator is
   * there only to put bit 7 in the carry so the rotate ON MEMORY can bring it back round into bit
   * 0. Two instructions to rotate a byte the 6502 cannot rotate in place.
   */
  void GalacticJump(Universe& _universe, Ports& _ports, SystemSeeds& _galaxy, SystemSeeds& _selected, JumpState& _jump,
                    ChartView& _chart) noexcept;

} // namespace Elite
