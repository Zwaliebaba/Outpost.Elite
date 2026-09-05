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

    screen.bubble.junk = 0u;             // 6502: JUNK
    _loop.control.dockingComputer = 0u;  // 6502: auto
    screen.status.ecmOurs = 0u;          // 6502: ECMP
    screen.status.midJump = 0u;          // 6502: MJ
    screen.status.cabinTemperature = 0u; // 6502: CABTMP
    screen.status.viewLaser = 0u;        // 6502: LAS2
    screen.status.missileArmed = 0u;     // 6502: MSAR
    screen.spaceView = 0u;               // 6502: VIEW
    screen.status.laserCount = 0u;       // 6502: LASCT
    screen.status.laserTemperature = 0u; // 6502: GNTMP
    screen.screen.hyperspaceEffect = 0u; // 6502: HFX
    screen.explosions = 0u;              // 6502: EV
    screen.message.delay = 0u;           // 6502: DLY
    screen.message.append = 0u;          // 6502: de
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

    screen.text.cellColour = TEXT_COLOUR_WHITE; // 6502: LDA #&10 / STA COL2
    _loop.clip.dontclip = 0u;                   // 6502: LDA #0 / STA dontclip
    screen.heaps.yx2M1 = SPACE_VIEW_LAST_ROW;   // 6502: LDA #2*Y-1 / STA Yx2M1

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
    ClearAllShips(screen.canvas, screen.draw, screen.heaps, screen.bubble, screen.work, screen.flight, screen.view);

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

  void DrawLaunchTunnel(FlightScreen& _screen, ClipState& _clip, TunnelEffects* _pacing) noexcept
  {
    // 6502: .LAUN LDY #sfxwhosh / JSR NOISE -- and the carry it returns is dropped, because the
    // next instruction is a load. §6.99's third answer costs nothing here.
    (void)_screen.effects.PlaySound(SOUND_MISSILE, false);

    // 6502: LDA #8 -- and `HFS2`'s first instruction, `STA STP`, is what receives it. This is the
    // only writer of the step on the launch path, and its absence is what §6.95 was working around.
    _screen.heaps.stp = LAUNCH_TUNNEL_STEP;

    /*
     * 6502: .HFS2 LDA QQ11 / PHA / LDA #0 / JSR TT66 / PLA / STA QQ11.
     *
     * The screen is cleared to a space view and the view type is then PUT BACK to whatever the
     * caller had. So the tunnel is drawn on a blank space view while the game still believes it is
     * showing the docked screen -- which is exactly right, because the caller has not finished
     * leaving it yet.
     */
    DrawTunnel(_screen, _clip, LAUNCH_TUNNEL_STEP, _pacing);
  }

  void DrawTunnel(FlightScreen& _screen, ClipState& _clip, std::uint8_t _step, TunnelEffects* _pacing) noexcept
  {
    // 6502: .HFS2 STA STP -- the only writer of the step on either tunnel's path, which is the
    // other half of §6.94's answer.
    _screen.heaps.stp = _step;

    /*
     * 6502: LDA QQ11 / PHA / LDA #0 / JSR TT66 / PLA / STA QQ11.
     *
     * The screen is cleared to a space view and the view type is then PUT BACK to whatever the
     * caller had. So the tunnel is drawn on a blank space view while the game still believes it is
     * showing the docked screen -- which is exactly right, because the caller has not finished
     * leaving it yet.
     */
    const std::uint8_t saved = _screen.view;
    SetUpScreen(_screen, 0u);
    _screen.view = saved;

    // 6502: falls into HFS1.
    DrawHyperspaceRings(_screen.canvas, _screen.heaps, _screen.draw, _screen.geometry, _screen.math, _clip, _pacing);
  }

  void DrawHyperspaceTunnel(FlightScreen& _screen, ClipState& _clip, DashboardEffects& _sound, TunnelEffects* _pacing) noexcept
  {
    /*
     * 6502: .HYPNOISE -- LDY #sfxhyp1 / LDA #&F5 / LDX #240 / JSR NOISE2, then `sfxwhosh` through
     * `NOISE`, then one frame of `DELAY`, then `sfxhyp1 + 128`.
     *
     * The last one is `NOISE`'s LAYERING entry: bit 7 of the effect number means "do not check
     * whether it is already playing", so the second hyperspace sound stacks on the first rather
     * than replacing it. That bit is the argument, not a separate routine.
     */
    (void)_sound.PlaySoundPitched(SOUND_HYPERSPACE, HYPERSPACE_SUSTAIN, HYPERSPACE_FREQUENCY);
    (void)_sound.PlaySound(SOUND_MISSILE, false);

    // 6502: LDY #1 / JSR DELAY -- one vertical sync, which is what the pacing object holds for.
    if (_pacing != nullptr)
    {
      _pacing->ShowFrame();
    }

    (void)_sound.PlaySound(static_cast<std::uint8_t>(SOUND_HYPERSPACE + 128u), false);

    // 6502: LDA #4 / JSR HFS2 / RTS.
    DrawTunnel(_screen, _clip, HYPERSPACE_TUNNEL_STEP, _pacing);
  }

  void Launch(FlightLoop& _loop, TunnelEffects* _pacing, std::uint8_t& _docked, std::uint8_t _crosshairX, std::uint8_t _crosshairY,
              std::uint8_t _techLevel, SystemSeeds& _selected) noexcept
  {
    LoopSpawnEffects spawning(_loop);
    FlightScreen& screen = _loop.screen;

    // 6502: LDX QQ12 / BEQ NLUNCH -- pressing "1" in flight does nothing but change the view.
    if (_docked != 0u)
    {
      // 6502: JSR LAUN, over the docked screen it is still showing.
      DrawLaunchTunnel(screen, _loop.clip, _pacing);
      ResetShipAndBubble(_loop); // 6502: JSR RES2

      /*
       * 6502: JSR TT111 -- for the SEEDS, not for the distance. The planet's look comes from the
       * system's own seeds through `tek`, so a launch has to know which system it is leaving.
       */
      const NearestSystem found = FindNearestSystem(screen.commander.GalaxySeeds(), _crosshairX, _crosshairY,
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
      (void)AddPlanetOrSun(screen.bubble, screen.work, spawning, _techLevel, screen.flight.blueprint);

      screen.work[8] = 128u;
      screen.work[7] = static_cast<std::uint8_t>(screen.work[7] + 1u);
      (void)AddStation(screen.bubble, screen.work, spawning, _techLevel, screen.flight.blueprint); // 6502: JSR NWSPS

      screen.flight.delta = LAUNCH_SPEED; // 6502: LDA #12 / STA DELTA

      // 6502: JSR BAD / ORA FIST / STA FIST -- the fine is levied by leaving, not by being scanned.
      screen.commander.At(Field::LegalStatus) =
        static_cast<std::uint8_t>(ContrabandPenalty(screen.commander) | screen.commander.At(Field::LegalStatus));

      screen.view = VIEW_LAUNCHING; // 6502: LDA #255 / STA QQ11

      /*
       * 6502: JSR HFS1 -- eight rings over the screen the tunnel left, and they erase themselves
       * because `LOOK1` below clears the screen anyway (§6.94).
       *
       * `STP` is still the 8 `LAUN` stored, which is the second half of §6.94's answer: the step
       * IS written on this path, by the routine the port had left as a stub (§6.109).
       */
      DrawHyperspaceRings(screen.canvas, screen.heaps, screen.draw, screen.geometry, screen.math, _loop.clip, _pacing);
    }

    // 6502: .NLUNCH LDX #0 / STX QQ12 / JMP LOOK1 -- and the X that clears the flag is the X the
    // view change is given, so a launch always ends looking forwards.
    _docked = 0u;
    ChangeView(screen, 0u);
  }

  std::uint8_t ShowTitleShip(TitleScreen& _title, std::uint8_t _token, std::uint8_t _shipType, std::uint8_t _distance) noexcept
  {
    FlightLoop& loop = _title.loop;
    FlightScreen& screen = loop.screen;

    // 6502: STY distaway / PHA / STX TYPE. The distance and the token are arguments here; `TYPE`
    // is a real byte and `NWSHP` below reads it back.
    screen.flight.type = _shipType;

    /*
     * 6502: LDA #&FF / STA MULIE / JSR RESET / LDA #0 / STA MULIE.
     *
     * The bracket is what keeps the theme playing. `RESET` is reached from the title screen with
     * the music already started, and `stopbd` opens `BIT MULIE / BMI itsoff` -- so the flag is how
     * one caller of `RESET` gets a different sound from every other.
     */
    screen.status.titleReset = 0xFFu;
    ResetGame(loop, _title.dockedFlag);
    screen.status.titleReset = 0u;

    _title.effects.ClearKeyLogger();          // 6502: JSR ZEKTRAN
    screen.effects.SetPalette(TITLE_PALETTE); // 6502: LDA #32 / JSR DOVDU19

    SetUpScreen(screen, TITLE_CLEAR_VIEW); // 6502: LDA #13 / JSR TT66
    screen.view = 0u;                      // 6502: LDA #0 / STA QQ11

    /*
     * 6502: LDA #96 / STA INWK+14 / LDA #96 / STA INWK+7 / LDX #127 / STX INWK+29 / STX INWK+30.
     *
     * `INWK+14` is the nose vector's z high byte, which `RES2`'s `ZINF` has just set to 96 WITH the
     * sign bit; writing 96 again clears that bit, so the ship faces away from the player rather
     * than towards them. The two 127s are the roll and pitch counters at maximum, and they are the
     * whole of why it turns: `MVEIT` steps the orientation by them on every frame.
     */
    screen.work[14] = TITLE_START_DISTANCE;
    screen.work[7] = TITLE_START_DISTANCE;
    screen.work[29] = TITLE_SPIN;
    screen.work[30] = TITLE_SPIN;

    // 6502: INX / STX QQ17 -- 128, which is sentence case, and it is what the prompt prints in.
    screen.printer.SetCaseFlags(0x80u);
    screen.text.caseFlags = 0x80u;

    // 6502: LDA TYPE / JSR NWSHP. The slot is kept because `LL9` needs the ship's block in `K%` as
    // well as the copy in `INWK` -- part 1 writes two bytes straight through `INF`.
    const NewShip created = AddShip(screen.bubble, screen.work, _shipType, screen.flight.blueprint);
    const std::uint8_t slot = created.created ? created.slot : std::uint8_t{0};

    screen.text.column = 6u;                               // 6502: LDA #6 / JSR DOXC
    PrintThenNewline(screen.printer, TITLE_HEADING_TOKEN); // 6502: LDA #30 / JSR plf
    screen.sink.Put(10u);                                  // 6502: LDA #10 / JSR TT26
    screen.text.column = 6u;                               // 6502: LDA #6 / JSR DOXC

    // 6502: LDA PATG / BEQ awe / LDA #13 / JSR DETOK -- the credits, and the byte that shows them
    // also changes what the main game loop spawns.
    if (_title.options.authorNames != 0u)
    {
      _title.tokens.Print(TITLE_AUTHORS_TOKEN);
    }

    /*
     * 6502: LDA brkd / BEQ BRBR2 -- and the branch it guards is REPLACED rather than omitted.
     *
     * `brkd` counts BRKs and the message it prints is read through `(&FD),Y`, a pointer the BRK
     * handler leaves behind. That handler is row 32's -- the C64's NMI vectors and Kernal setup,
     * which the ledger marks Replace and this port has no equivalent for -- so `brkd` is zero for
     * the life of the process and the branch is not reachable rather than not written.
     */

    // 6502: .BRBR2 LDY #0 / STY DELTA / STY JSTK.
    screen.flight.delta = 0u;
    _title.options.joystick = 0u;

    screen.text.row = TITLE_PROMPT_ROW;     // 6502: LDA #15 / STA YC
    screen.text.column = TITLE_PROMPT_LEFT; // 6502: LDA #1 / STA XC
    _title.tokens.Print(_token);            // 6502: PLA / JSR DETOK -- the caller's own token

    screen.text.column = 3u;                 // 6502: LDA #3 / JSR DOXC
    _title.tokens.Print(TITLE_BYLINE_TOKEN); // 6502: LDA #12 / JSR DETOK

    screen.math.cnt2 = TITLE_CNT2;              // 6502: LDA #12 / STA CNT2
    screen.flight.mainLoopCounter = TITLE_MCNT; // 6502: LDA #5 / STA MCNT
    _title.options.joystick = 0xFFu;            // 6502: LDA #&FF / STA JSTK

    for (;;)
    {
      // 6502: .TLL2 LDA INWK+7 / CMP #1 / BEQ TL1 / DEC INWK+7 -- the ship closes and then holds.
      if (screen.work[7] != 1u)
      {
        screen.work[7] = static_cast<std::uint8_t>(screen.work[7] - 1u);
      }

      // 6502: .TL1 JSR MVEIT.
      MoveShip(screen.canvas, screen.draw, screen.work, screen.math, screen.flight, loop.tactics, screen.flight.blueprint, screen.view);

      /*
       * 6502: LDX distaway / STX INWK+6 / LDA MCNT / AND #3 / LDA #0 / STA INWK / STA INWK+3.
       *
       * Three stores that undo what `MVEIT` just did to the position, so the ship turns on the
       * spot: the z low byte goes back to the caller's distance and both coordinate low bytes to
       * zero. The `LDA MCNT / AND #3` between them is DEAD -- `LDA #0` overwrites the accumulator
       * before anything can read it.
       */
      screen.work[6] = _distance;
      screen.work[0] = 0u;
      screen.work[3] = 0u;

      // 6502: JSR LL9.
      DrawShip(screen.canvas, screen.draw, screen.geometry, screen.math, loop.clip, loop.projection, screen.work,
               screen.bubble.blocks[slot], loop.heap, screen.flight.blueprint, screen.flight.type, loop.drawing);

      // 6502: JSR RDKEY / DEC MCNT.
      const TitleKey scan = _title.effects.ScanTitleKeys(_title.keys);
      screen.flight.mainLoopCounter = static_cast<std::uint8_t>(screen.flight.mainLoopCounter - 1u);

      /*
       * 6502: BIT KY7 / BMI TL3 / BCC TLL2 / INC JSTK.
       *
       * Fire returns with `JSTK` still &FF; any other key runs the `INC` first and leaves it zero.
       * The prompt reads as a choice of two equal ways to continue and is actually the joystick
       * question.
       */
      if ((_title.keys[KEY_FIRE] & 0x80u) != 0u)
      {
        return scan.key;
      }
      if (scan.pressed)
      {
        _title.options.joystick = static_cast<std::uint8_t>(_title.options.joystick + 1u);
        return scan.key;
      }
    }
  }

  void PrepareDeathScene(FlightLoop& _loop, DashboardEffects& _sound) noexcept
  {
    FlightScreen& screen = _loop.screen;

    // 6502: JSR EXNO3 -- `LDY #sfxexpl / BNE NOISE`, and the carry is whatever killed us.
    (void)_sound.PlaySound(SOUND_EXPLOSION, false);

    ResetShipAndBubble(_loop); // 6502: JSR RES2

    // 6502: ASL DELTA / ASL DELTA -- and the upstream comment says "divide by 4", which is the
    // BBC's `LSR`. This build SHIFTS LEFT twice, so the speed is multiplied (§6.117).
    screen.flight.delta = static_cast<std::uint8_t>(screen.flight.delta << 2);

    /*
     * 6502: LDX #24 / JSR DET1 / JSR TT66.
     *
     * `DET1` is a bare `RTS` here, so the `LDX #24` goes nowhere and A is NOT set to 6 -- what
     * `TT66` gets is whatever `RES2` left in it (§6.117). `DEATH_VIEW` is that byte, measured.
     */
    SetUpScreen(screen, DEATH_VIEW);

    // 6502: JSR BOX -- the SAME border again, and `BOX2` EORs, so drawing it twice rubs it out.
    DrawFullBorder(screen.canvas, screen.draw);

    // 6502: LDA #0 / STA SCBASE+&1F1F / STA SCBASE+&118 -- the two bytes `BOX` STORES instead of
    // EORing, which a second pass therefore cannot remove.
    screen.canvas.Write(BOTTOM_RIGHT_CORNER, 0u);
    screen.canvas.Write(BORDER_TOP_RIGHT, 0u);

    // 6502: JSR nWq -- a whole new stardust field over the cleared screen.
    SeedStardustField(screen.canvas, screen.draw, screen.dust, screen.rng, false);

    // 6502: LDA #12 / JSR DOYC / JSR DOXC -- the cursor, then the sign.
    screen.text.row = GAME_OVER_ROW;
    screen.text.column = GAME_OVER_COLUMN;
    screen.printer.PrintPhrase(GAME_OVER_TOKEN); // 6502: LDA #146 / JSR ex

    /*
     * 6502: .D1 -- spawn wreckage until the fifth slot is taken.
     *
     * `LDA FRIN+4 / BEQ D1` is the condition, so this fills slots 0 to 4 -- five pieces, not four,
     * and the loop tests the slot AFTER creating one.
     */
    do
    {
      const RngResult roll = SeedDebris(screen.work, screen.rng); // 6502: JSR Ze

      screen.work[0] = static_cast<std::uint8_t>(roll.value >> 2); // 6502: LSR A / LSR A / STA INWK

      // 6502: LDY #0 / STY QQ11 / STY INWK+1 / STY INWK+4 / STY INWK+7 / STY INWK+32.
      screen.view = 0u;
      screen.work[1] = 0u;
      screen.work[4] = 0u;
      screen.work[7] = 0u;
      screen.work[32] = 0u;

      // 6502: DEY / STY MCNT -- 255, so every timer-based call in the loop is stopped.
      screen.flight.mainLoopCounter = 0xFFu;

      // 6502: EOR #%00101010 / STA INWK+3 / ORA #%01010000 / STA INWK+6.
      const std::uint8_t flipped = static_cast<std::uint8_t>(screen.work[0] ^ 0x2Au);
      screen.work[3] = flipped;
      screen.work[6] = static_cast<std::uint8_t>(flipped | 0x50u);

      // 6502: TXA / AND #%10001111 / STA INWK+29 -- a gentle roll, sign kept.
      screen.work[29] = static_cast<std::uint8_t>(roll.previous & 0x8Fu);

      screen.status.laserCount = DEATH_FRAMES; // 6502: LDY #64 / STY LASCT

      // 6502: SEC / ROR A / AND #%10000111 / STA INWK+30 -- and the `A` is the roll byte above,
      // not the random one: `TXA` left it there.
      const std::uint8_t pitched = static_cast<std::uint8_t>((screen.work[29] >> 1) | 0x80u);
      screen.work[30] = static_cast<std::uint8_t>(pitched & 0x87u);

      /*
       * 6502: LDX #OIL / LDA XX21-1+2*PLT / BEQ D3 / BCC D3 / DEX.
       *
       * The load has no brackets, so it reads the byte AT `XX21 + 7` rather than through it --
       * always &D0, never zero, so the `BEQ` is dead and only the carry decides.
       *
       * AND THAT CARRY IS NOT `Ze`'S, whatever the upstream comment says ("which will be random
       * following the above call to Ze"). `SEC / ROR A` sits between them, four instructions up:
       * the `ROR` shifts A right and puts A's OLD BIT 0 into the carry, and A there is the roll
       * counter, `X AND %10001111`. So the wreckage is a plate when the random X was odd, which is
       * random but is a different random number from the one the comment names (§6.117).
       */
      const bool plate = (roll.previous & 1u) != 0u;
      const std::uint8_t type = plate ? SHIP_TYPE_ALLOY_PLATE : SHIP_TYPE_CANISTER;

      const NewShip made = AddDebris(screen.bubble, screen.work, type, screen.flight.delta, screen.flight.blueprint);

      // 6502: JSR DORND / AND #%10000000 / LDY #31 / STA (INF),Y -- half the wreckage is already
      // dead, which is what makes some of it explode as it goes past.
      const RngResult state = screen.rng.Next(false);
      if (made.created)
      {
        screen.bubble.blocks[made.slot][31] = static_cast<std::uint8_t>(state.value & 0x80u);
      }
    } while (screen.bubble.slots[DEATH_DEBRIS_SLOT] == 0u);
  }

  void Die(FlightLoop& _loop, DashboardEffects& _sound) noexcept
  {
    FlightScreen& screen = _loop.screen;

    PrepareDeathScene(_loop, _sound);

    ClearFlightKeys(_loop.keys); // 6502: JSR U%

    // 6502: STA DELTA -- and A is the zero `U%` left in it, so we stop dead.
    screen.flight.delta = 0u;

    // 6502: JSR M% / JSR NOSPRITES / .D2 JSR M% / DEC LASCT / BNE D2.
    (void)MainFlightLoop(_loop);
    HideAllSprites(screen.sight);

    do
    {
      (void)MainFlightLoop(_loop);
      screen.status.laserCount = static_cast<std::uint8_t>(screen.status.laserCount - 1u);
    } while (screen.status.laserCount != 0u);

    // 6502: LDX #31 / JSR DET1 / JMP DEATH2 -- the first is a bare RTS and the second is the
    // caller's own death exit, which `Main.cpp` already wires as `RES2` then `BR1` (§6.25).
  }

} // namespace Elite
