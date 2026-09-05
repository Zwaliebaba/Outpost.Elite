#pragma once

#include "Charts.h"
#include "FlightLoop.h"
#include "Market.h"
#include "StartUp.h"
#include "Universe.h"

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
   * `JSR MESS` and continues into `jmp`; and `TT18` ends `INC QQ11` and continues into `TT110`,
   * which is the launch. None of the three is a call.
   */

  /*
   * 6502: hyp1 -- arrive at the selected system, and fall into `GVL` to stock its market.
   *
   * `_findNearest` is the difference between the two entry points the game uses. `hyp1` opens
   * `JSR TT111`, which puts the system nearest the crosshairs into `QQ15`; `TT18` jumps to
   * `hyp1+3`, three bytes past it, because the chart has already chosen and calling it again would
   * pick the same system a second time for nothing. Two entries into one routine, distinguished by
   * a flag rather than by a second function, because there is only one routine.
   *
   * `STX EV` with X zero is the encounter counter reset -- arriving somewhere new means the
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
  void ArriveAtSystem(CommanderBlock& _commander, CurrentSystem& _current, SystemSeeds& _selected, const SystemSeeds& _target,
                      SystemData& _described, MarketState& _market, Rng& _rng, std::uint8_t& _explosionCount, std::uint8_t _crosshairX,
                      std::uint8_t _crosshairY, const SystemSeeds& _galaxy, bool _findNearest) noexcept;

  /*
   * 6502: MJP -- witchspace, which is a jump that did not arrive.
   *
   * `LDA #3 / JSR TT66` clears to the space view, `LL164` draws the tunnel, `RES2` resets the
   * bubble, and then `STY MJ` sets the flag that stops the spawner and refuses the fuel scoop.
   * **Y is whatever `RES2` left**, not a value this routine chose -- so the port passes the byte
   * rather than assuming a 1, and the oracle is what says which it is.
   *
   * `MJP1` spawns Thargoid pairs until there are more than three of them, and the `LDA #3` it
   * compares against is then stored into `NOSTM`: witchspace has three specks of dust instead of
   * the usual eighteen, and the constant is shared between the two on purpose.
   */
  void EnterWitchspace(FlightLoop& _loop, CommanderBlock& _commander, DashboardEffects& _sound, TunnelEffects* _pacing) noexcept;

  /*
   * 6502: ptg -- `LSR COK / SEC / ROL COK`, and then it FALLS INTO `MJP`.
   *
   * That is `ORA #1` and not a rotate: the `LSR` and the `ROL` cancel, the `SEC` forces bit 0, and
   * bit 7 survives because the `LSR` shifted it down and the `ROL` shifted it back. The same shape
   * as the `ASL / SEC / ROR` that §6.126 found mis-ported twice, one bit the other way round.
   *
   * `COK` is the competition flags byte, so holding the configuration key through a jump is
   * recorded in the commander file for ever.
   */
  void EnterWitchspaceCheating(FlightLoop& _loop, CommanderBlock& _commander, DashboardEffects& _sound, TunnelEffects* _pacing) noexcept;

  /*
   * What `TT18` did, which the original says by WHERE IT ENDS UP -- and that is four places.
   *
   * Three of them look alike from inside the routine and are not. `BNE RTS111` returns having
   * drawn nothing; `BNE TT114` JUMPS OUT to redraw the chart, which is a different screen's job
   * and not a return at all; and the fall-through past `INC QQ11` is the launch. The port had the
   * first two as one outcome and the oracle disagreed about the generator on the short-range
   * chart, because the original had gone off to draw it.
   */
  enum class JumpResult : std::uint8_t
  {
    Arrived,     ///< 6502: the fall-through into `TT110` -- the caller launches
    Witchspace,  ///< 6502: BCS MJP -- three bytes in 256 miss the system
    NoRedraw,    ///< 6502: BNE RTS111 -- the view's low six bits are set, so nothing is drawn
    RedrawChart, ///< 6502: BNE TT114 -- a chart is up, and the caller redraws it
  };

  /*
   * 6502: TT18 -- spend the fuel and go, and it is the whole jump.
   *
   * `LDA QQ14 / SEC / SBC QQ8 / BCS P%+4 / LDA #0 / STA QQ14`: the fuel minus the distance, and
   * `BCS P%+4` steps over the two-byte `LDA #0`, so a jump that costs more than you have leaves
   * you with none rather than with a wrapped byte. The check that it is affordable happened in
   * `hyp` (slice 2d); this is the arithmetic.
   *
   * `JSR CTRL / AND PATG / BMI ptg` is the cheat: holding the key with the configuration option on
   * forces witchspace. Then one roll in 256 -- `CMP #253 / BCS MJP` -- does it anyway.
   */
  [[nodiscard]] JumpResult PerformJump(FlightLoop& _loop, CurrentSystem& _current, SystemSeeds& _selected, JumpState& _jump,
                                       SystemData& _described, MarketState& _market, DashboardEffects& _sound, TunnelEffects* _pacing,
                                       std::uint8_t _crosshairX, std::uint8_t _crosshairY, const SystemSeeds& _galaxy, bool _controlHeld,
                                       bool _patg) noexcept;

  /*
   * 6502: Ghy -- the galactic hyperdrive, which moves you a galaxy on and forgets your crimes.
   *
   * `LDX GHYP / BEQ zZ+1` is the one to read twice. `zZ` is `LDA #96`, which assembles as `A9 60`,
   * so `zZ+1` is the OPERAND -- and &60 is `RTS`. With no drive fitted the branch jumps into the
   * middle of an instruction and executes its argument as a return. §6.121's rule about idioms
   * that look like something else, in its purest form: there is no code at `zZ+1`.
   *
   * `INX / STX GHYP / STX FIST` is why the drive is single-use and why it cleans your record: X was
   * 255 and becomes 0, and the same zero goes into both bytes.
   *
   * `.G1 LDA QQ21,X / ASL A / ROL QQ21,X` rotates each of the galaxy's six seed bytes left by one
   * -- the `ASL A` is there only to put bit 7 in the carry so the `ROL` on MEMORY can bring it back
   * round into bit 0. Two instructions to rotate a byte the 6502 cannot rotate in place.
   */
  void GalacticJump(FlightLoop& _loop, CurrentSystem& _current, SystemSeeds& _galaxy, SystemSeeds& _selected, JumpState& _jump,
                    ChartView& _chart, TunnelEffects* _pacing) noexcept;

} // namespace Elite
