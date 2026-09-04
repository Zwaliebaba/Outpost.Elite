#include "pch.h"

#include "Flight.h"

#include "Combat.h"
#include "Market.h"
#include "PlanetDraw.h"
#include "Spawn.h"

namespace Elite
{
namespace
{
/// 6502: NOST -- twelve in this build, whatever three versions' comments say (§6.44).
inline constexpr std::uint8_t STARDUST_COUNT = 12;

/// 6502: LDA #128 / STA JSTY -- the middle of a control's range, and the sign both rate bytes
/// carry when the ship is not turning.
inline constexpr std::uint8_t CONTROL_CENTRE = 128;

/// 6502: LDA #3 -- the roll and the drift a launch starts with, from ONE load into three bytes.
inline constexpr std::uint8_t LAUNCH_ROLL = 3;

/// 6502: LDA #2*Y-1 -- the bottom row `CHKON` counts as on screen, which `TT23` moves to 199 and
/// this puts back.
inline constexpr std::uint8_t SPACE_VIEW_LAST_ROW = 143;

/// 6502: LDA #255 / STA QQ11 -- the view `TT110` leaves behind, which is not a view at all: it
/// is what makes `LOOK1` take its "the view is changing" branch rather than its "already there"
/// one (§6.81's finding, from the other side).
inline constexpr std::uint8_t VIEW_LAUNCHING = 255;
} // namespace

void ClearBubbleState(FlightLoop& _loop) noexcept
{
  FlightScreen& screen = _loop.screen;

  // 6502: FRIN and MANY -- the slots and the per-type counts, which `SSPR` is part of (§6.58).
  for (std::size_t slot = 0; slot < screen.bubble.slots.size(); ++slot)
  {
    screen.bubble.slots[slot] = 0u;
  }
  for (std::size_t type = 0; type < screen.bubble.counts.size(); ++type)
  {
    screen.bubble.counts[type] = 0u;
  }

  screen.bubble.junk = 0u;              // 6502: JUNK
  _loop.control.dockingComputer = 0u;   // 6502: auto
  screen.status.ecmOurs = 0u;           // 6502: ECMP
  screen.status.midJump = 0u;           // 6502: MJ
  screen.status.cabinTemperature = 0u;  // 6502: CABTMP
  screen.status.viewLaser = 0u;         // 6502: LAS2
  screen.status.missileArmed = 0u;      // 6502: MSAR
  screen.spaceView = 0u;                // 6502: VIEW
  screen.status.laserCount = 0u;        // 6502: LASCT
  screen.status.laserTemperature = 0u;  // 6502: GNTMP
  screen.screen.hyperspaceEffect = 0u;  // 6502: HFX
  screen.explosions = 0u;               // 6502: EV
  screen.message.delay = 0u;            // 6502: DLY
  screen.message.append = 0u;           // 6502: de
}

void ResetShipAndBubble(FlightLoop& _loop) noexcept
{
  FlightScreen& screen = _loop.screen;

  _loop.effects.StopDockingMusic(); // 6502: JSR stopbd

  /*
   * 6502: LDA BOMB / BPL BOMBOK / JSR BOMBOFF / STA BOMB.
   *
   * `STA BOMB` stores what `BOMBOFF` left in A, which is the zero its own last instruction
   * loaded -- so the bomb is switched off by the routine and emptied by its accumulator, and
   * reading `STA BOMB` as "store the bomb" gets the value from the wrong routine.
   */
  if ((screen.commander.At(Field::EnergyBomb) & 0x80u) != 0u)
  {
    StopEnergyBomb(screen.screen);
    screen.commander.At(Field::EnergyBomb) = 0u;
  }

  screen.dust.count = STARDUST_COUNT; // 6502: LDA #NOST / STA NOSTM

  // 6502: LDX #&FF / STX LSX2 / STX LSY2 / STX MSTG -- both halves of the ball heap and the lock.
  screen.heaps.ball[0] = 0xFFu;
  screen.heaps.ball[BALL_HEAP_SIZE] = 0xFFu;
  screen.bubble.missileTarget = 0xFFu;

  /*
   * 6502: LDA #128 / STA JSTY / STA ALP2 / STA BET2 / ASL A / STA BETA / ...
   *
   * AND `JSTX` IS NOT HERE. The pitch rate is re-centred and the roll rate is not, and neither is
   * inside `ZERO`'s range -- so a launch inherits whatever roll the last flight ended on while
   * the pitch always starts straight.
   */
  _loop.control.pitch = CONTROL_CENTRE;
  screen.flight.alp2 = CONTROL_CENTRE;
  screen.flight.bet2 = CONTROL_CENTRE;

  // 6502: ASL A -- 128 doubles to zero, which is where the next six stores get their value.
  screen.flight.beta = 0u;
  screen.flight.bet1 = 0u;
  screen.flight.alp2Next = 0u;
  screen.flight.bet2Next = 0u;
  screen.flight.mainLoopCounter = 0u;
  screen.trumbleSprites = 0u;

  // 6502: LDA #3 / STA DELTA / STA ALPHA / STA ALP1 -- one load, three meanings.
  screen.flight.delta = LAUNCH_ROLL;
  screen.flight.alpha = LAUNCH_ROLL;
  screen.flight.alp1 = LAUNCH_ROLL;

  screen.text.cellColour = TEXT_COLOUR_WHITE;      // 6502: LDA #&10 / STA COL2
  _loop.clip.dontclip = 0u;                        // 6502: LDA #0 / STA dontclip
  screen.heaps.yx2M1 = SPACE_VIEW_LAST_ROW;        // 6502: LDA #2*Y-1 / STA Yx2M1

  // 6502: LDA SSPR / BEQ P%+5 / JSR SPBLB -- the station bulb is a TOGGLE, so this puts it out
  // only because it was lit, and the test is what keeps the two in step.
  if (screen.bubble.counts[SHIP_TYPE_STATION] != 0u)
  {
    ToggleStationIndicator(screen.canvas);
  }

  // 6502: LDA ECMA / BEQ yu / JSR ECMOF.
  if (screen.status.ecmCountdown != 0u)
  {
    StopEcm(screen.canvas, screen.status, _loop.effects);
  }

  // 6502: .yu JSR WPSHPS -- rub every ship off the screen and forget both line heaps.
  ClearAllShips(screen.canvas, screen.draw, screen.heaps, screen.bubble, screen.work,
                screen.flight, screen.view);

  ClearBubbleState(_loop); // 6502: JSR ZERO

  // 6502: LDA #LO(LS%) / STA SLSP / LDA #HI(LS%) / STA SLSP+1 -- the heap is empty again.
  screen.bubble.heapBottom = SHIP_HEAP_TOP;

  ClearShipBlock(screen.work); // 6502: and no RTS -- it falls into ZINF
}

void ResetGame(FlightLoop& _loop, std::uint8_t& _docked) noexcept
{
  FlightScreen& screen = _loop.screen;

  ClearBubbleState(_loop); // 6502: JSR ZERO, which leaves A at zero for the loop below

  /*
   * 6502: LDX #6 / .SAL3 STA BETA,X / DEX / BPL SAL3.
   *
   * Seven bytes from `BETA` upwards, and in THIS build that is the pitch pair, both hyperspace
   * counters, `ECMA` and the roll's two sign bytes -- not the text cursor the upstream comment
   * names, which is the BBC's layout at those addresses.
   */
  screen.flight.beta = 0u;
  screen.flight.bet1 = 0u;
  screen.status.hyperspaceCountdown = 0u;
  screen.status.hyperspaceCounter = 0u;
  screen.status.ecmCountdown = 0u;
  screen.flight.alp1 = 0u;
  screen.flight.alp2 = 0u;

  /*
   * 6502: TXA / STA QQ12 / LDX #2 / .REL5 STA FSH,X / DEX / BPL REL5.
   *
   * X is 255 because the loop above ran off its end, and that 255 does TWO jobs: it is the flag
   * that says "docked", and it is the value the three shield and energy bytes are filled with.
   * The second only works because a full bank happens to be 255.
   */
  _docked = 0xFFu;
  screen.status.forwardShield = 0xFFu;
  screen.status.aftShield = 0xFFu;
  screen.status.energy = 0xFFu;

  ResetShipAndBubble(_loop); // 6502: and no RTS -- it falls into RES2
}

void Launch(FlightLoop& _loop, StartUpEffects& _start, SpawnEffects& _spawn,
            std::uint8_t& _docked, std::uint8_t _crosshairX, std::uint8_t _crosshairY,
            std::uint8_t _techLevel, SystemSeeds& _selected) noexcept
{
  FlightScreen& screen = _loop.screen;

  // 6502: LDX QQ12 / BEQ NLUNCH -- pressing "1" in flight does nothing but change the view.
  if (_docked != 0u)
  {
    _start.ShowDockingTunnel();  // 6502: JSR LAUN, over the docked screen it is still showing
    ResetShipAndBubble(_loop);   // 6502: JSR RES2

    /*
     * 6502: JSR TT111 -- for the SEEDS, not for the distance. The planet's look comes from the
     * system's own seeds through `tek`, so a launch has to know which system it is leaving.
     */
    const NearestSystem found =
      FindNearestSystem(screen.commander.GalaxySeeds(), _crosshairX, _crosshairY,
                        screen.commander.At(Field::SystemX), screen.commander.At(Field::SystemY));
    _selected = found.seeds;

    /*
     * 6502: INC INWK+8 / JSR SOS1 / LDA #128 / STA INWK+8 / INC INWK+7 / JSR NWSPS.
     *
     * ONE ZEROED BLOCK, TWO SPAWNS, THREE BYTES BETWEEN THEM. `ZINF` left `INWK` clear, so the
     * planet goes in with a z sign of one -- ahead and very close -- and the station follows with
     * the sign flipped to 128 and the high byte at one, which puts it behind and further off.
     * The station is what you have just left, and this is where it goes.
     */
    screen.work[8] = static_cast<std::uint8_t>(screen.work[8] + 1u);
    (void)AddPlanetOrSun(screen.bubble, screen.work, _spawn, _techLevel);

    screen.work[8] = 128u;
    screen.work[7] = static_cast<std::uint8_t>(screen.work[7] + 1u);
    _loop.effects.SpawnStation();

    screen.flight.delta = LAUNCH_SPEED; // 6502: LDA #12 / STA DELTA

    // 6502: JSR BAD / ORA FIST / STA FIST -- the fine is levied by leaving, not by being scanned.
    screen.commander.At(Field::LegalStatus) = static_cast<std::uint8_t>(
      ContrabandPenalty(screen.commander) | screen.commander.At(Field::LegalStatus));

    screen.view = VIEW_LAUNCHING; // 6502: LDA #255 / STA QQ11

    // 6502: JSR HFS1 -- eight rings over the screen the tunnel left, and they erase themselves
    // because `LOOK1` below clears the screen anyway (§6.94).
    DrawHyperspaceRings(screen.canvas, screen.heaps, screen.draw, screen.geometry, screen.math,
                        _loop.clip);
  }

  // 6502: .NLUNCH LDX #0 / STX QQ12 / JMP LOOK1 -- and the X that clears the flag is the X the
  // view change is given, so a launch always ends looking forwards.
  _docked = 0u;
  ChangeView(screen, 0u);
}

} // namespace Elite
