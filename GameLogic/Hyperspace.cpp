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

    /// The view `MJP` and a completed jump both clear to.
    constexpr std::uint8_t SPACE_VIEW = 3;

    /// One constant doing two jobs -- the Thargoid count witchspace stops at, and the
    /// dust count, which is why witchspace has exactly as many specks as Thargoids to make.
    constexpr std::uint8_t WITCHSPACE_THARGOIDS = 3;

    /// Three bytes in 256 miss the system entirely.
    constexpr std::uint8_t WITCHSPACE_ROLL = 253;

    /// The "GALACTIC HYPERSPACE" token, through `MESS`.
    constexpr std::uint8_t GALACTIC_MESSAGE = 116;

    /// The middle of the galaxy, where `Ghy` puts you: both crosshair bytes.
    constexpr std::uint8_t GALAXY_CENTRE = 96;

    /// Into MJ -- `ZINF`'s loop counter, which reaches &FF and is never reloaded.
    constexpr std::uint8_t WITCHSPACE_FLAG = 0xFFu;

    /*
     * A Thargoid made, then the count tested; and the test is BACKWARDS from the
     * way it reads. Comparing 3 against the count sets the carry when 3 >= count, so the loop
     * runs while there are three or fewer and stops on the FOURTH. A bubble that refuses a
     * Thargoid would spin here for ever, which is why `NWSHP`'s answer being ignored by `GTHG`
     * (§6.135) matters: the loop is bounded by the count, not by success.
     */
    void FillWitchspaceWithThargoids(Bubble& _bubble, Ship& _work, Rng& _rng, const Blueprint*& _blueprint, bool _carryIn) noexcept
    {
      bool carry = _carryIn;
      do
      {
        carry = SpawnThargoidPair(_bubble, _work, _rng, _blueprint, carry).created;
      } while (WITCHSPACE_THARGOIDS >= _bubble.Count(ShipType::Thargoid));
    }

  } // namespace

  void ArriveAtSystem(Universe& _universe, SystemSeeds& _selected, const SystemSeeds& _target, SystemData& _described,
                      MarketState& _market, std::uint8_t _crosshairX, std::uint8_t _crosshairY, const SystemSeeds& _galaxy,
                      bool _findNearest) noexcept
  {
    /*
     * TT111 -- and `TT18` enters at `hyp1+3`, three bytes past it, because the chart has
     * already chosen. The same routine with and without its first instruction.
     */
    std::uint8_t crosshairX = _crosshairX;
    std::uint8_t crosshairY = _crosshairY;

    if (_findNearest)
    {
      const NearestSystem nearest =
        FindNearestSystem(_galaxy, crosshairX, crosshairY, _universe.commander.systemX, _universe.commander.systemY);
      _selected = nearest.seeds;

      // `TT111` ends by jumping to `TT24`, so `QQ3` to `QQ5` are its side effect and not its answer.
      _described = nearest.data;

      /*
       * `TT111` SNAPS THE CROSSHAIRS onto the system it found, both bytes.
       *
       * So the `jmp` below cannot be handed the pair the caller passed in: on this entry it reads
       * the snapped one, on the `hyp1+3` entry it reads whatever the chart left. The port passed
       * the caller's through on both and disagreed with the oracle about `QQ0` at the first
       * position tried, because 20,173 is not a system -- it is merely near one.
       */
      crosshairX = nearest.x;
      crosshairY = nearest.y;
    }

    // jmp -- where you are becomes where the crosshairs are, which is what arriving
    // means.
    CurrentSystemToCrosshairs(_universe.commander, crosshairX, crosshairY);

    // The six seeds the countdown saved, because the player has been moving the
    // crosshairs ever since.
    _universe.current.seeds = _target;

    // X came out of the loop at &FF and stepping it makes zero, so arriving resets the
    // spawner's rate limit (§6.135). One instruction doing two things.
    _universe.explosions = 0;

    /*
     * The economy, tech level and government copied out of the seed bytes.
     *
     * The economy, tech level and government cached from the seeds the system was just given.
     * `TT111` filled `QQ3` to `QQ5` as a side effect of finding the system, so these read what it
     * left rather than recomputing them -- and on the `hyp1+3` path what they read is what the
     * CHART's last `TT111` left, which is the same system.
     */
    _universe.current.economy = _described.economy;
    _universe.current.techLevel = _described.techLevel;
    _universe.current.government = _described.government;

    // It FALLS INTO GVL -- and `GVL` reads `QQ28`, which is what was just stored, so the
    // market comes from the DESCRIBED system too and not from the seeds in `QQ2`.
    GenerateMarket(_universe.rng, _described.economy, _market);
  }

  void EnterWitchspace(Universe& _universe, Ports& _ports, Commander& _commander) noexcept
  {

    // The space view, the tunnel, then `RES2`.
    SetUpScreen(_universe, _ports, SPACE_VIEW);
    DrawHyperspaceTunnel(_universe, _ports);
    ResetShipAndBubble(_universe, _ports);

    /*
     * Into MJ -- and Y is whatever `RES2` left, which is not a value this routine chose.
     *
     * `RES2` falls into `ZINF`, whose clearing loop counts Y down from the block's size --
     * so it exits with Y at &FF, and the witchspace flag is 255. Three routines away from
     * the instruction that stores it, and nothing in between touches Y. A port that wrote 1 would
     * behave identically (everything tests `MJ` for non-zero) and would still be wrong in the
     * commander file and in every oracle comparison.
     */
    _universe.status.midJump = WITCHSPACE_FLAG;

    // .MJP1 -- Thargoids until there are four, and then the same 3 becomes the dust count.
    FillWitchspaceWithThargoids(_universe.bubble, _universe.work, _universe.rng, _universe.flight.blueprint, false);
    _universe.dust.count = WITCHSPACE_THARGOIDS;

    // LOOK1 on view 0 -- the forward view, drawn over what the tunnel left.
    ChangeView(_universe, _ports, 0u);

    /*
     * The y coordinate's low five bits complemented, so leaving witchspace does not put
     * you back where you were. Five bits, so it is a jump of at most 31.
     */
    _commander.systemY = static_cast<std::uint8_t>(_commander.systemY ^ 0x1Fu);
  }

  void EnterWitchspaceCheating(Universe& _universe, Ports& _ports, Commander& _commander) noexcept
  {
    /*
     * Shift out, set the carry, rotate back: which sets bit 0 and is NOT a rotate
     * of the byte.
     *
     * The first shift moves every bit down and drops bit 0 into the carry; setting the carry
     * throws that away; the rotate moves every bit back up and brings the 1 in at the bottom.
     * Bit 7 survives the round trip. §6.126 found the mirror of this -- the same trio the other
     * way up, which sets bit 7 -- ported as a shift twice over, so it is written out here rather
     * than transcribed instruction by instruction.
     */
    _commander.competition = static_cast<std::uint8_t>(_commander.competition | 1u);

    // And then it FALLS INTO MJP.
    EnterWitchspace(_universe, _ports, _commander);
  }

  JumpResult PerformJump(Universe& _universe, Ports& _ports, SystemSeeds& _selected, JumpState& _jump, SystemData& _described,
                         MarketState& _market,  std::uint8_t _crosshairX,
                         std::uint8_t _crosshairY, const SystemSeeds& _galaxy, bool _controlHeld, bool _authorNames) noexcept
  {

    /*
     * The jump's cost taken off the tank, and a borrow clamped to empty.
     *
     * The branch steps over the two-byte load, so a borrow -- a jump costing more fuel than
     * you have -- leaves the tank EMPTY rather than wrapped. `hyp` refused the jump for that in
     * slice 2d, so this is the arithmetic and not the check, and the clamp is unreachable in a
     * game that goes through `hyp`. Transcribed anyway, because what makes it unreachable is
     * another routine.
     */
    const FuelBurn fuel = _universe.commander.fuel.Burned(static_cast<std::uint8_t>(_jump.distance & 0xFFu));
    _universe.commander.fuel = fuel.left;

    // The tunnel is only drawn from a space view. Jumping with a chart up spends
    // the fuel and shows nothing.
    const bool fromSpace = _universe.view == 0u;

    /*
     * THE CARRY THE ROLL BELOW ROTATES IN, and it comes from two different places.
     *
     * The view test, `CTRL`, the mask against `PATG` and the branch to `ptg` all leave the flag
     * alone, so the generator after them sees either the fuel subtraction's carry (the chart
     * path, which skips the tunnel) or whatever `LL164` returned (the space path).
     *
     * `LL164` ALWAYS RETURNS WITH IT SET, and that is provable rather than measured: it is
     * `HYPNOISE` and then `HFS2`, and `HFS2` has two exits -- the doubling that carries out of
     * the byte, taken only with the carry set, and the fall-through past the test against 160,
     * which is reached only when that branch is not taken and therefore only with the carry
     * set. Both roads out
     * only when the branch is not taken and therefore only with the carry set. Both roads out
     * carry a one. Confirmed against the interpreter, which stops at the roll with the flag set.
     */
    bool carry = fuel.carry;

    if (fromSpace)
    {
      SetUpScreen(_universe, _ports, _universe.view);
      DrawHyperspaceTunnel(_universe, _ports);
      carry = true;
    }

    // `CTRL` masked against `PATG`: the configuration key and the option together.
    if (_controlHeld && _authorNames)
    {
      EnterWitchspaceCheating(_universe, _ports, _universe.commander);
      return JumpResult::Witchspace;
    }

    // A random byte of 253 or more, and three bytes in 256 miss.
    const RngResult roll = _universe.rng.Next(carry);
    if (roll.value >= WITCHSPACE_ROLL)
    {
      EnterWitchspace(_universe, _ports, _universe.commander);
      return JumpResult::Witchspace;
    }

    // Past the call to `TT111`, because the chart has already chosen.
    ArriveAtSystem(_universe, _selected, _jump.target, _described, _market, _crosshairX, _crosshairY, _galaxy, false);

    // RES2 then SOLAR -- a clean bubble and then the system's own planet and sun.
    ResetShipAndBubble(_universe, _ports);

    // The four seams it reached through are four routines this library now
    // contains, so M3-b-1 has it call them.
    BuildSystem(_universe, _ports, false);

    /*
     * The view masked to six bits, and anything left goes straight back.
     *
     * Six bits, so views 64 and above -- the charts, which set bit 7 or bit 6 -- come out zero and
     * fall through, and any other non-space view returns. That is not "is this a space view": it
     * is "is this a view whose low six bits are clear", and the two differ for exactly the screens
     * the charts use.
     */
    if ((_universe.view & 0x3Fu) != 0u)
    {
      return JumpResult::NoRedraw;
    }

    // TTX66, then TT114 on a non-zero view; otherwise the view steps up and it falls into `TT110`.
    SetUpScreenPixels(_universe.canvas, _universe.draw, _universe.text, _universe.screen, _universe.bubble, _universe.flight,
                      _universe.status, _universe.commander.fuel, _universe.compass, _universe.video,
                      _universe.memoryMap, _universe.view, &_universe.picture);

    if (_universe.view != 0u)
    {
      // TT114 -- and that is a jump OUT of this routine into the chart's own redraw,
      // not a return. The caller does it, the way it does the launch below.
      return JumpResult::RedrawChart;
    }

    ++_universe.view; // The view stepped up, and the fall-through into `TT110` is the caller's
    return JumpResult::Arrived;
  }

  void GalacticJump(Universe& _universe, Ports& _ports, SystemSeeds& _galaxy, SystemSeeds& _selected, JumpState& _jump,
                    ChartView& _chart) noexcept
  {

    /*
     * No drive fitted branches to `zZ+1` -- and there is no code at `zZ+1`.
     *
     * `zZ` loads 96, assembled as `A9 60`, so the branch lands on the OPERAND and executes &60
     * as an `RTS`. With no drive fitted the routine returns from the middle of an instruction.
     */
    if (_universe.commander.galacticDrive == 0u)
    {
      return;
    }

    // X was 255 and stepping it makes zero, so both bytes become zero: the drive is spent
    // and the record is clean, from one register.
    _universe.commander.galacticDrive = 0u;
    _universe.commander.legalStatus = 0u;

    // WW2 with a two -- the countdown started at two rather than fifteen, and `wW2` stores
    // the same value into `QQ22` as well as `QQ22+1` (§6.159).
    _jump.countdown = 2u;
    _jump.counter = 2u;

    // The galaxy number stepped up and masked -- eight galaxies, and the mask is what
    // wraps the eighth back to the first.
    _universe.commander.galaxyNumber = static_cast<std::uint8_t>((_universe.commander.galaxyNumber + 1u) & 0xF7u);

    /*
     * Each of the six seed bytes rotated left by one.
     *
     * It takes two instructions because the 6502 cannot rotate memory through its own bit 7:
     * doubling a COPY is only there to put that bit in the carry so the rotate on the byte
     * itself can bring it round.
     */
    for (int index = 5; index >= 0; --index)
    {
      std::uint8_t& byte = _galaxy.bytes[static_cast<std::size_t>(index)];
      const ShiftResult high = RotateLeftValue(byte, false);
      byte = RotateLeftValue(byte, high.carry).value;
    }

    // The crosshairs to the middle of the new galaxy, both bytes.
    _chart.cursorX = GALAXY_CENTRE;
    _chart.cursorY = GALAXY_CENTRE;

    // TT110 -- and this is the LAUNCH, called for its redraw: a galactic jump from a
    // chart leaves you in space looking forward.
    Launch(_universe, _ports, _chart.cursorX, _chart.cursorY, _selected);

    // TT111, then the six seeds copied into `safehouse` -- the system nearest the middle
    // of the galaxy becomes both the selection and the countdown's target.
    const NearestSystem nearest =
      FindNearestSystem(_galaxy, _chart.cursorX, _chart.cursorY, _universe.commander.systemX, _universe.commander.systemY);
    _selected = nearest.seeds;
    _jump.target = nearest.seeds;

    // And `TT111` writes `QQ9` and `QQ10` again, so the crosshairs end up on the system it
    // found rather than on the 96,96 four instructions above. The same snap `hyp1` depends on.
    _chart.cursorX = nearest.x;
    _chart.cursorY = nearest.y;

    // Both distance bytes zeroed, so the distance is zero and the countdown has nowhere
    // to go.
    _jump.distance = 0u;

    /*
     * The message token, and then it FALLS INTO `jmp` and returns.
     *
     * AND THAT IS ALL IT DOES. `Ghy` does not call `hyp1`, so a galactic jump does NOT cache the
     * new system's economy or stock its market: `QQ28`, `tek`, `gov` and `AVL` still describe the
     * galaxy you left until the countdown `wW2` just started runs out and `TT18` arrives properly.
     * The port had an `ArriveAtSystem` here, on the reasoning that arriving somewhere ought to
     * stock its market. The routine says otherwise and the routine wins.
     */
    ShowMessage(_universe.canvas, _ports.printer, _universe.text, _universe.sentences, _universe.message, GALACTIC_MESSAGE,
                _universe.view, &_universe.picture);
    CurrentSystemToCrosshairs(_universe.commander, _chart.cursorX, _chart.cursorY);
  }

} // namespace Elite
