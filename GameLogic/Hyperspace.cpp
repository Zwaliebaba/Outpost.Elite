#include "pch.h"

#include "Hyperspace.h"

#include "Flight.h"
#include "Messages.h"
#include "Spawn.h"
#include "GameLoop.h"
#include "ViewChange.h"

namespace Elite
{

  namespace
  {

    /// 6502: LDA #3 / JSR TT66 -- the view `MJP` and a completed jump both clear to.
    constexpr std::uint8_t SPACE_VIEW = 3;

    /// 6502: LDA #3 / CMP MANY+THG / BCS MJP1 and STA NOSTM -- one constant doing two jobs, which
    /// is why witchspace has exactly as many dust specks as it has Thargoids to make.
    constexpr std::uint8_t WITCHSPACE_THARGOIDS = 3;

    /// 6502: CMP #253 / BCS MJP -- three bytes in 256 miss the system entirely.
    constexpr std::uint8_t WITCHSPACE_ROLL = 253;

    /// 6502: LDA #116 / JSR MESS -- the "GALACTIC HYPERSPACE" token.
    constexpr std::uint8_t GALACTIC_MESSAGE = 116;

    /// 6502: LDA #96 / STA QQ9 / STA QQ10 -- the middle of the galaxy, where `Ghy` puts you.
    constexpr std::uint8_t GALAXY_CENTRE = 96;

    /// 6502: STY MJ -- `ZINF`'s loop counter, which reaches &FF and is never reloaded.
    constexpr std::uint8_t WITCHSPACE_FLAG = 0xFFu;

    /*
     * 6502: .MJP1 JSR GTHG / LDA #3 / CMP MANY+THG / BCS MJP1 -- and the test is BACKWARDS from
     * the way it reads. `CMP #3` against the count sets the carry when 3 >= count, so the loop
     * runs while there are three or fewer and stops on the FOURTH. A bubble that refuses a
     * Thargoid would spin here for ever, which is why `NWSHP`'s answer being ignored by `GTHG`
     * (§6.135) matters: the loop is bounded by the count, not by success.
     */
    void FillWitchspaceWithThargoids(Bubble& _bubble, ShipBlock& _work, Rng& _rng, std::uint16_t& _blueprint, bool _carryIn) noexcept
    {
      bool carry = _carryIn;
      do
      {
        carry = SpawnThargoidPair(_bubble, _work, _rng, _blueprint, carry).created;
      } while (WITCHSPACE_THARGOIDS >= _bubble.counts[SHIP_TYPE_THARGOID]);
    }

  } // namespace

  void ArriveAtSystem(CommanderBlock& _commander, CurrentSystem& _current, SystemSeeds& _selected, const SystemSeeds& _target,
                      SystemData& _described, MarketState& _market, Rng& _rng, std::uint8_t& _explosionCount, std::uint8_t _crosshairX,
                      std::uint8_t _crosshairY, const SystemSeeds& _galaxy, bool _findNearest) noexcept
  {
    /*
     * 6502: JSR TT111 -- and `TT18` enters at `hyp1+3`, three bytes past it, because the chart has
     * already chosen. The same routine with and without its first instruction.
     */
    std::uint8_t crosshairX = _crosshairX;
    std::uint8_t crosshairY = _crosshairY;

    if (_findNearest)
    {
      const NearestSystem nearest =
        FindNearestSystem(_galaxy, crosshairX, crosshairY, _commander.At(Field::SystemX), _commander.At(Field::SystemY));
      _selected = nearest.seeds;

      // 6502: `TT111` ends `JMP TT24`, so `QQ3` to `QQ5` are its side effect and not its answer.
      _described = nearest.data;

      /*
       * 6502: STA QQ10 / STA QQ9 -- `TT111` SNAPS THE CROSSHAIRS onto the system it found.
       *
       * So the `jmp` below cannot be handed the pair the caller passed in: on this entry it reads
       * the snapped one, on the `hyp1+3` entry it reads whatever the chart left. The port passed
       * the caller's through on both and disagreed with the oracle about `QQ0` at the first
       * position tried, because 20,173 is not a system -- it is merely near one.
       */
      crosshairX = nearest.x;
      crosshairY = nearest.y;
    }

    // 6502: JSR jmp -- LDA QQ9 / STA QQ0 / LDA QQ10 / STA QQ1: where you are becomes where the
    // crosshairs are, which is what arriving means.
    CurrentSystemToCrosshairs(_commander, crosshairX, crosshairY);

    // 6502: LDX #5 / .TT112 LDA safehouse,X / STA QQ2,X / DEX / BPL TT112 -- the seeds the
    // countdown saved, because the player has been moving the crosshairs ever since.
    _current.seeds = _target;

    // 6502: INX / STX EV -- X came out of the loop at &FF and the `INX` makes it zero, so arriving
    // resets the spawner's rate limit (§6.135). One instruction doing two things.
    _explosionCount = 0;

    /*
     * 6502: LDA QQ3 / STA QQ28 / LDA QQ5 / STA tek / LDA QQ4 / STA gov.
     *
     * The economy, tech level and government cached from the seeds the system was just given.
     * `TT111` filled `QQ3` to `QQ5` as a side effect of finding the system, so these read what it
     * left rather than recomputing them -- and on the `hyp1+3` path what they read is what the
     * CHART's last `TT111` left, which is the same system.
     */
    _current.economy = _described.economy;
    _current.techLevel = _described.techLevel;
    _current.government = _described.government;

    // 6502: it FALLS INTO GVL -- and `GVL` reads `QQ28`, which is what was just stored, so the
    // market comes from the DESCRIBED system too and not from the seeds in `QQ2`.
    GenerateMarket(_rng, _described.economy, _market);
  }

  void EnterWitchspace(FlightLoop& _loop, CommanderBlock& _commander, DashboardEffects& _sound, TunnelEffects* _pacing) noexcept
  {
    FlightScreen& screen = _loop.screen;

    // 6502: LDA #3 / JSR TT66 / JSR LL164 / JSR RES2.
    SetUpScreen(screen, SPACE_VIEW);
    DrawHyperspaceTunnel(screen, _loop.clip, _sound, _pacing);
    ResetShipAndBubble(_loop);

    /*
     * 6502: STY MJ -- and Y is whatever `RES2` left, which is not a value this routine chose.
     *
     * `RES2` falls into `ZINF`, whose clearing loop is `LDY #NI%-1 / .ZI1 STA INWK,Y / DEY / BPL
     * ZI1` -- so it exits with Y at &FF, and the witchspace flag is 255. Three routines away from
     * the instruction that stores it, and nothing in between touches Y. A port that wrote 1 would
     * behave identically (everything tests `MJ` for non-zero) and would still be wrong in the
     * commander file and in every oracle comparison.
     */
    screen.status.midJump = WITCHSPACE_FLAG;

    // 6502: .MJP1 -- Thargoids until there are four, and then the same 3 becomes the dust count.
    FillWitchspaceWithThargoids(screen.bubble, screen.work, screen.rng, screen.flight.blueprint, false);
    screen.dust.count = WITCHSPACE_THARGOIDS;

    // 6502: LDX #0 / JSR LOOK1 -- the forward view, drawn over what the tunnel left.
    ChangeView(screen, 0u);

    /*
     * 6502: LDA QQ1 / EOR #%00011111 / STA QQ1 -- the y coordinate is scrambled, so leaving
     * witchspace does not put you back where you were. Five bits, so it is a jump of at most 31.
     */
    _commander.At(Field::SystemY) = static_cast<std::uint8_t>(_commander.At(Field::SystemY) ^ 0x1Fu);
  }

  void EnterWitchspaceCheating(FlightLoop& _loop, CommanderBlock& _commander, DashboardEffects& _sound, TunnelEffects* _pacing) noexcept
  {
    /*
     * 6502: .ptg LSR COK / SEC / ROL COK -- which is `ORA #1` and NOT a rotate.
     *
     * The `LSR` moves every bit down and drops bit 0 into the carry; the `SEC` throws that away;
     * the `ROL` moves every bit back up and brings the 1 in at the bottom. Bit 7 survives the round
     * trip. §6.126 found the mirror of this (`ASL / SEC / ROR` is `ORA #128`) ported as a shift
     * twice over, so it is written out here rather than transcribed instruction by instruction.
     */
    _commander.At(Field::Competition) = static_cast<std::uint8_t>(_commander.At(Field::Competition) | 1u);

    // 6502: and then it FALLS INTO MJP.
    EnterWitchspace(_loop, _commander, _sound, _pacing);
  }

  JumpResult PerformJump(FlightLoop& _loop, CurrentSystem& _current, SystemSeeds& _selected, JumpState& _jump, SystemData& _described,
                         MarketState& _market, DashboardEffects& _sound, TunnelEffects* _pacing, std::uint8_t _crosshairX,
                         std::uint8_t _crosshairY, const SystemSeeds& _galaxy, bool _controlHeld, bool _patg) noexcept
  {
    FlightScreen& screen = _loop.screen;

    /*
     * 6502: LDA QQ14 / SEC / SBC QQ8 / BCS P%+4 / LDA #0 / STA QQ14.
     *
     * `BCS P%+4` steps over the two-byte `LDA #0`, so a borrow -- a jump costing more fuel than
     * you have -- leaves the tank EMPTY rather than wrapped. `hyp` refused the jump for that in
     * slice 2d, so this is the arithmetic and not the check, and the clamp is unreachable in a
     * game that goes through `hyp`. Transcribed anyway, because what makes it unreachable is
     * another routine.
     */
    const SubResult fuel = SubtractWithCarry(screen.commander.At(Field::Fuel), static_cast<std::uint8_t>(_jump.distance & 0xFFu), true);
    screen.commander.At(Field::Fuel) = fuel.carry ? fuel.value : std::uint8_t{0};

    // 6502: LDA QQ11 / BNE ee5 / JSR TT66 / JSR LL164 -- the tunnel is only drawn from a space
    // view. Jumping with a chart up spends the fuel and shows nothing.
    const bool fromSpace = screen.view == 0u;

    /*
     * THE CARRY THE ROLL BELOW ROTATES IN, and it comes from two different places.
     *
     * `LDA QQ11 / BNE ee5`, `JSR CTRL`, `AND PATG` and `BMI ptg` all leave the flag alone, so the
     * `JSR DORND` after them sees either the fuel `SBC`'s carry (the chart path, which skips the
     * tunnel) or whatever `LL164` returned (the space path).
     *
     * `LL164` ALWAYS RETURNS WITH IT SET, and that is provable rather than measured: it is
     * `HYPNOISE` and then `HFS2`, and `HFS2` has two exits -- `ASL K / BCS HF8`, which is taken
     * only with the carry set, and the fall-through past `CMP #160 / BCC HFL2`, which is reached
     * only when the branch is not taken and therefore only with the carry set. Both roads out
     * carry a one. Confirmed against the interpreter, which stops at the roll with the flag set.
     */
    bool carry = fuel.carry;

    if (fromSpace)
    {
      SetUpScreen(screen, screen.view);
      DrawHyperspaceTunnel(screen, _loop.clip, _sound, _pacing);
      carry = true;
    }

    // 6502: .ee5 JSR CTRL / AND PATG / BMI ptg -- the configuration key and the option together.
    if (_controlHeld && _patg)
    {
      EnterWitchspaceCheating(_loop, screen.commander, _sound, _pacing);
      return JumpResult::Witchspace;
    }

    // 6502: JSR DORND / CMP #253 / BCS MJP -- and three bytes in 256 miss.
    const RngResult roll = screen.rng.Next(carry);
    if (roll.value >= WITCHSPACE_ROLL)
    {
      EnterWitchspace(_loop, screen.commander, _sound, _pacing);
      return JumpResult::Witchspace;
    }

    // 6502: JSR hyp1+3 -- past the `JSR TT111`, because the chart has already chosen.
    ArriveAtSystem(screen.commander, _current, _selected, _jump.target, _described, _market, screen.rng, screen.explosions, _crosshairX,
                   _crosshairY, _galaxy, false);

    // 6502: JSR RES2 / JSR SOLAR -- a clean bubble and then the system's own planet and sun.
    ResetShipAndBubble(_loop);

    // 6502: JSR SOLAR -- and `SpawnEffects` is `LoopSpawnEffects` over the loop, the adapter
    // FlightLoop.h already carries for exactly this (it was a seam when `Spawn.cpp` was written).
    LoopSpawnEffects spawning(_loop);
    BuildSystem(screen.canvas, screen.draw, screen.dust, screen.heaps, screen.bubble, screen.work, screen.commander, screen.rng,
                screen.flight, spawning, _current.techLevel, _current.seeds.bytes, screen.view, false);

    /*
     * 6502: LDA QQ11 / AND #%00111111 / BNE RTS111.
     *
     * Six bits, so views 64 and above -- the charts, which set bit 7 or bit 6 -- come out zero and
     * fall through, and any other non-space view returns. That is not "is this a space view": it
     * is "is this a view whose low six bits are clear", and the two differ for exactly the screens
     * the charts use.
     */
    if ((screen.view & 0x3Fu) != 0u)
    {
      return JumpResult::NoRedraw;
    }

    // 6502: JSR TTX66 / LDA QQ11 / BNE TT114 / INC QQ11, and then it falls into `TT110`.
    SetUpScreenPixels(screen.canvas, screen.draw, screen.math, screen.geometry, screen.text, screen.screen, screen.bubble, screen.flight,
                      screen.status, screen.commander.At(Field::Fuel), screen.compass, screen.sight, screen.view);

    if (screen.view != 0u)
    {
      // 6502: BNE TT114 -- and that is a jump OUT of this routine into the chart's own redraw,
      // not a return. The caller does it, the way it does the launch below.
      return JumpResult::RedrawChart;
    }

    ++screen.view; // 6502: INC QQ11 -- and the fall-through into `TT110` is the caller's
    return JumpResult::Arrived;
  }

  void GalacticJump(FlightLoop& _loop, CurrentSystem& _current, SystemSeeds& _galaxy, SystemSeeds& _selected, JumpState& _jump,
                    ChartView& _chart, TunnelEffects* _pacing) noexcept
  {
    FlightScreen& screen = _loop.screen;
    static_cast<void>(_pacing);

    /*
     * 6502: LDX GHYP / BEQ zZ+1 -- and there is no code at `zZ+1`.
     *
     * `zZ` is `LDA #96`, assembled as `A9 60`, so the branch lands on the OPERAND and executes &60
     * as an `RTS`. With no drive fitted the routine returns from the middle of an instruction.
     */
    if (screen.commander.At(Field::GalacticDrive) == 0u)
    {
      return;
    }

    // 6502: INX / STX GHYP / STX FIST -- X was 255, so both bytes become zero: the drive is spent
    // and the record is clean, from one register.
    screen.commander.At(Field::GalacticDrive) = 0u;
    screen.commander.At(Field::LegalStatus) = 0u;

    // 6502: LDA #2 / JSR wW2 -- the countdown, started at two rather than fifteen.
    _jump.countdown = 2u;

    // 6502: INC GCNT / LDA GCNT / AND #%11110111 / STA GCNT -- eight galaxies, and the mask is
    // what wraps the eighth back to the first.
    screen.commander.At(Field::GalaxyNumber) = static_cast<std::uint8_t>((screen.commander.At(Field::GalaxyNumber) + 1u) & 0xF7u);

    /*
     * 6502: .G1 LDA QQ21,X / ASL A / ROL QQ21,X / DEX / BPL G1.
     *
     * Each of the six seed bytes rotated left by one, and it takes two instructions because the
     * 6502 cannot rotate memory through its own bit 7: the `ASL A` on a COPY is only there to put
     * that bit in the carry so the `ROL` on the byte itself can bring it round.
     */
    for (int index = 5; index >= 0; --index)
    {
      std::uint8_t& byte = _galaxy.bytes[static_cast<std::size_t>(index)];
      const ShiftResult high = RotateLeftValue(byte, false);
      byte = RotateLeftValue(byte, high.carry).value;
    }

    // 6502: .zZ LDA #96 / STA QQ9 / STA QQ10 -- the crosshairs to the middle of the new galaxy.
    _chart.cursorX = GALAXY_CENTRE;
    _chart.cursorY = GALAXY_CENTRE;

    // 6502: JSR TT110 -- and this is the LAUNCH, called for its redraw: a galactic jump from a
    // chart leaves you in space looking forward.
    Launch(_loop, _pacing, _jump.docked, _chart.cursorX, _chart.cursorY, _current.techLevel, _selected);

    // 6502: JSR TT111 / LDX #5 / .dumdeedum LDA QQ15,X / STA safehouse,X -- the system nearest the
    // middle of the galaxy becomes both the selection and the countdown's target.
    const NearestSystem nearest =
      FindNearestSystem(_galaxy, _chart.cursorX, _chart.cursorY, screen.commander.At(Field::SystemX), screen.commander.At(Field::SystemY));
    _selected = nearest.seeds;
    _jump.target = nearest.seeds;

    // 6502: and `TT111` writes `QQ9` and `QQ10` again, so the crosshairs end up on the system it
    // found rather than on the 96,96 four instructions above. The same snap `hyp1` depends on.
    _chart.cursorX = nearest.x;
    _chart.cursorY = nearest.y;

    // 6502: LDX #0 / STX QQ8 / STX QQ8+1 -- both bytes, so the distance is zero and the countdown
    // has nowhere to go.
    _jump.distance = 0u;

    /*
     * 6502: LDA #116 / JSR MESS, and then it FALLS INTO `jmp` and returns.
     *
     * AND THAT IS ALL IT DOES. `Ghy` does not call `hyp1`, so a galactic jump does NOT cache the
     * new system's economy or stock its market: `QQ28`, `tek`, `gov` and `AVL` still describe the
     * galaxy you left until the countdown `wW2` just started runs out and `TT18` arrives properly.
     * The port had an `ArriveAtSystem` here, on the reasoning that arriving somewhere ought to
     * stock its market. The routine says otherwise and the routine wins.
     */
    ShowMessage(screen.canvas, screen.printer, screen.text, screen.extended, screen.message, GALACTIC_MESSAGE, screen.view);
    CurrentSystemToCrosshairs(screen.commander, _chart.cursorX, _chart.cursorY);
  }

} // namespace Elite
