#include "pch.h"

#include "Flight.h"

#include "Scanner.h"

#include "Combat.h"
#include "Market.h"
#include "Music.h"
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

  void ClearBubbleState(Universe& _universe, Ports& _ports) noexcept
  {
    // 6502: FRIN and MANY -- the slots and the per-type counts, which `SSPR` is part of (§6.58).
    for (std::uint8_t& slot : _universe.bubble.slots)
    {
      slot = 0u;
    }
    for (std::uint8_t& count : _universe.bubble.counts)
    {
      count = 0u;
    }

    _universe.bubble.junk = 0u;             // 6502: JUNK
    _universe.control.dockingComputer = 0u; // 6502: auto
    _universe.status.ecmOurs = 0u;          // 6502: ECMP
    _universe.status.midJump = 0u;          // 6502: MJ
    _universe.status.cabinTemperature = 0u; // 6502: CABTMP
    _universe.status.viewLaser = 0u;        // 6502: LAS2
    _universe.status.missileArmed = 0u;     // 6502: MSAR
    _universe.spaceView = 0u;               // 6502: VIEW
    _universe.status.laserCount = 0u;       // 6502: LASCT
    _universe.status.laserTemperature = 0u; // 6502: GNTMP
    _universe.screen.hyperspaceEffect = 0u; // 6502: HFX
    _universe.explosions = 0u;              // 6502: EV
    _universe.message.delay = 0u;           // 6502: DLY
    _universe.message.append = 0u;          // 6502: de
  }

  void ResetShipAndBubble(Universe& _universe, Ports& _ports) noexcept
  {
    // 6502: JSR stopbd
    StopDockingMusic(_universe.music, _universe.status.titleReset, _universe.sound, _universe.memoryMap, _ports.sid);

    /*
     * 6502: LDA BOMB / BPL BOMBOK / JSR BOMBOFF / STA BOMB.
     *
     * `STA BOMB` stores what `BOMBOFF` left in A, which is the zero its own last instruction
     * loaded -- so the bomb is switched off by the routine and emptied by its accumulator, and
     * reading `STA BOMB` as "store the bomb" gets the value from the wrong routine.
     */
    if ((_universe.commander.energyBomb & 0x80u) != 0u)
    {
      StopEnergyBomb(_universe.screen);
      _universe.commander.energyBomb = 0u;
    }

    _universe.dust.count = STARDUST_COUNT; // 6502: LDA #NOST / STA NOSTM

    // 6502: LDX #&FF / STX LSX2 / STX LSY2 / STX MSTG -- both halves of the ball heap and the lock.
    _universe.heaps.ball[0] = 0xFFu;
    _universe.heaps.ball[BALL_HEAP_SIZE] = 0xFFu;
    _universe.bubble.missileTarget = 0xFFu;

    /*
     * 6502: LDA #128 / STA JSTY / STA ALP2 / STA BET2 / ASL A / STA BETA / ...
     *
     * AND `JSTX` IS NOT HERE. The pitch rate is re-centred and the roll rate is not, and neither is
     * inside `ZERO`'s range -- so a launch inherits whatever roll the last flight ended on while
     * the pitch always starts straight.
     */
    _universe.control.pitch = CONTROL_CENTRE;
    _universe.flight.alp2 = CONTROL_CENTRE;
    _universe.flight.bet2 = CONTROL_CENTRE;

    // 6502: ASL A -- 128 doubles to zero, which is where the next six stores get their value.
    _universe.flight.beta = 0u;
    _universe.flight.bet1 = 0u;
    _universe.flight.alp2Next = 0u;
    _universe.flight.bet2Next = 0u;
    _universe.flight.mainLoopCounter = 0u;
    _universe.trumbles.count = 0u;

    // 6502: LDA #3 / STA DELTA / STA ALPHA / STA ALP1 -- one load, three meanings.
    _universe.flight.delta = LAUNCH_ROLL;
    _universe.flight.alpha = LAUNCH_ROLL;
    _universe.flight.alp1 = LAUNCH_ROLL;

    _universe.text.palette = TEXT_COLOUR_WHITE; // 6502: LDA #&10 / STA COL2
    _universe.clip.dontclip = 0u;                  // 6502: LDA #0 / STA dontclip
    _universe.heaps.yx2M1 = SPACE_VIEW_LAST_ROW;   // 6502: LDA #2*Y-1 / STA Yx2M1

    // 6502: LDA SSPR / BEQ P%+5 / JSR SPBLB -- the station bulb is a TOGGLE, so this puts it out
    // only because it was lit, and the test is what keeps the two in step.
    if (_universe.bubble.Count(ShipType::Station) != 0u)
    {
      ToggleStationIndicator(_universe.canvas);
    }

    // 6502: LDA ECMA / BEQ yu / JSR ECMOF.
    if (_universe.status.ecmCountdown != 0u)
    {
      StopEcm(_universe.canvas, _universe.status, _universe.sound);
    }

    // 6502: .yu JSR WPSHPS -- rub every ship off the screen and forget both line heaps.
    ClearAllShips(_universe.canvas, _universe.heaps, _universe.bubble, _universe.work, _universe.flight, _universe.view);

    ClearBubbleState(_universe, _ports); // 6502: JSR ZERO

    // 6502: LDA #LO(LS%) / STA SLSP / LDA #HI(LS%) / STA SLSP+1 -- the heap is empty again.
    _universe.bubble.heapBottom = HeapOffset::Top();

    ClearShip(_universe.work); // 6502: and no RTS -- it falls into ZINF
  }

  void ResetGame(Universe& _universe, Ports& _ports) noexcept
  {
    ClearBubbleState(_universe, _ports); // 6502: JSR ZERO, which leaves A at zero for the loop below

    /*
     * 6502: LDX #6 / .SAL3 STA BETA,X / DEX / BPL SAL3.
     *
     * Seven bytes from `BETA` upwards, and in THIS build that is the pitch pair, both hyperspace
     * counters, `ECMA` and the roll's two sign bytes -- not the text cursor the upstream comment
     * names, which is the BBC's layout at those addresses.
     */
    _universe.flight.beta = 0u;
    _universe.flight.bet1 = 0u;
    _universe.status.hyperspaceCountdown = 0u;
    _universe.status.hyperspaceCounter = 0u;
    _universe.status.ecmCountdown = 0u;
    _universe.flight.alp1 = 0u;
    _universe.flight.alp2 = 0u;

    /*
     * 6502: TXA / STA QQ12 / LDX #2 / .REL5 STA FSH,X / DEX / BPL REL5.
     *
     * X is 255 because the loop above ran off its end, and that 255 does TWO jobs: it is the flag
     * that says "docked", and it is the value the three shield and energy bytes are filled with.
     * The second only works because a full bank happens to be 255.
     */
    _universe.dockedFlag = 0xFFu;
    _universe.status.forwardShield = 0xFFu;
    _universe.status.aftShield = 0xFFu;
    _universe.status.energy = 0xFFu;

    ResetShipAndBubble(_universe, _ports); // 6502: and no RTS -- it falls into RES2
  }

  void DrawLaunchTunnel(Universe& _universe, Ports& _ports) noexcept
  {
    // 6502: .LAUN LDY #sfxwhosh / JSR NOISE -- and the carry it returns is dropped, because the
    // next instruction is a load. §6.99's third answer costs nothing here.
    (void)PlaySoundEffect(_universe.sound, SoundEffect::Missile, false);

    // 6502: LDA #8 -- and `HFS2`'s first instruction, `STA STP`, is what receives it. This is the
    // only writer of the step on the launch path, and its absence is what §6.95 was working around.
    _universe.heaps.stp = LAUNCH_TUNNEL_STEP;

    /*
     * 6502: .HFS2 LDA QQ11 / PHA / LDA #0 / JSR TT66 / PLA / STA QQ11.
     *
     * The screen is cleared to a space view and the view type is then PUT BACK to whatever the
     * caller had. So the tunnel is drawn on a blank space view while the game still believes it is
     * showing the docked screen -- which is exactly right, because the caller has not finished
     * leaving it yet.
     */
    DrawTunnel(_universe, _ports, LAUNCH_TUNNEL_STEP);
  }

  void DrawTunnel(Universe& _universe, Ports& _ports, std::uint8_t _step) noexcept
  {
    // 6502: .HFS2 STA STP -- the only writer of the step on either tunnel's path, which is the
    // other half of §6.94's answer.
    _universe.heaps.stp = _step;

    /*
     * 6502: LDA QQ11 / PHA / LDA #0 / JSR TT66 / PLA / STA QQ11.
     *
     * The screen is cleared to a space view and the view type is then PUT BACK to whatever the
     * caller had. So the tunnel is drawn on a blank space view while the game still believes it is
     * showing the docked screen -- which is exactly right, because the caller has not finished
     * leaving it yet.
     */
    const std::uint8_t saved = _universe.view;
    SetUpScreen(_universe, _ports, 0u);
    _universe.view = saved;

    // 6502: falls into HFS1.
    DrawHyperspaceRings(_universe.canvas, _universe.heaps, _universe.geometry, _universe.math, _universe.clip, _ports.present);
  }

  void DrawHyperspaceTunnel(Universe& _universe, Ports& _ports) noexcept
  {
    /*
     * 6502: .HYPNOISE -- LDY #sfxhyp1 / LDA #&F5 / LDX #240 / JSR NOISE2, then `sfxwhosh` through
     * `NOISE`, then one frame of `DELAY`, then `sfxhyp1 + 128`.
     *
     * The last one is `NOISE`'s LAYERING entry: bit 7 of the effect number means "do not check
     * whether it is already playing", so the second hyperspace sound stacks on the first rather
     * than replacing it. That bit is the argument, not a separate routine.
     */
    /*
     * The carry into `NOISE2` here is the CALLER's -- `LDY`, `LDA` and `LDX` touch no flag -- and
     * it is unobservable, which is why false is passed rather than threaded through `LL164`.
     * `NOISE` reads it on one path only, `LDA DNOIZ / BNE SOUR1`, where it becomes the RETURN
     * value; nothing it writes depends on it, and `HYPNOISE` discards the answer.
     */
    (void)PlaySoundEffectPitched(_universe.sound, SoundEffect::Hyperspace, HYPERSPACE_SUSTAIN, HYPERSPACE_FREQUENCY, false);
    (void)PlaySoundEffect(_universe.sound, SoundEffect::Missile, false);

    // 6502: LDY #1 / JSR DELAY -- one vertical sync, which is what the pacing object holds for.
    _ports.present.Present();

    (void)PlaySoundEffect(_universe.sound, SoundEffect::HyperspaceAgain, false);

    // 6502: LDA #4 / JSR HFS2 / RTS.
    DrawTunnel(_universe, _ports, HYPERSPACE_TUNNEL_STEP);
  }

  void Launch(Universe& _universe, Ports& _ports, std::uint8_t _crosshairX, std::uint8_t _crosshairY, SystemSeeds& _selected) noexcept
  {
    // 6502: LDX QQ12 / BEQ NLUNCH -- pressing "1" in flight does nothing but change the view.
    if (_universe.dockedFlag != 0u)
    {
      // 6502: JSR LAUN, over the docked screen it is still showing.
      DrawLaunchTunnel(_universe, _ports);
      ResetShipAndBubble(_universe, _ports); // 6502: JSR RES2

      /*
       * 6502: JSR TT111 -- for the SEEDS, not for the distance. The planet's look comes from the
       * system's own seeds through `tek`, so a launch has to know which system it is leaving.
       */
      const NearestSystem found = FindNearestSystem(_universe.commander.galaxySeeds, _crosshairX, _crosshairY, _universe.commander.systemX,
                                                    _universe.commander.systemY);
      _selected = found.seeds;

      /*
       * 6502: INC INWK+8 / JSR SOS1 / LDA #128 / STA INWK+8 / INC INWK+7 / JSR NWSPS.
       *
       * ONE ZEROED BLOCK, TWO SPAWNS, THREE BYTES BETWEEN THEM. `ZINF` left `INWK` clear, so the
       * planet goes in with a z sign of one -- ahead and very close -- and the station follows with
       * the sign flipped to 128 and the high byte at one, which puts it behind and further off.
       * The station is what you have just left, and this is where it goes.
       */
      _universe.work.z.sgn = static_cast<std::uint8_t>(_universe.work.z.sgn + 1u);
      (void)AddPlanetOrSun(_universe, _ports);

      _universe.work.z.sgn = 128u;
      _universe.work.z.hi = static_cast<std::uint8_t>(_universe.work.z.hi + 1u);
      (void)AddStation(_universe, _ports); // 6502: JSR NWSPS

      _universe.flight.delta = LAUNCH_SPEED; // 6502: LDA #12 / STA DELTA

      // 6502: JSR BAD / ORA FIST / STA FIST -- the fine is levied by leaving, not by being scanned.
      _universe.commander.legalStatus = static_cast<std::uint8_t>(ContrabandPenalty(_universe.commander) | _universe.commander.legalStatus);

      _universe.view = VIEW_LAUNCHING; // 6502: LDA #255 / STA QQ11

      /*
       * 6502: JSR HFS1 -- eight rings over the screen the tunnel left, and they erase themselves
       * because `LOOK1` below clears the screen anyway (§6.94).
       *
       * `STP` is still the 8 `LAUN` stored, which is the second half of §6.94's answer: the step
       * IS written on this path, by the routine the port had left as a stub (§6.109).
       */
      DrawHyperspaceRings(_universe.canvas, _universe.heaps, _universe.geometry, _universe.math, _universe.clip, _ports.present);
    }

    // 6502: .NLUNCH LDX #0 / STX QQ12 / JMP LOOK1 -- and the X that clears the flag is the X the
    // view change is given, so a launch always ends looking forwards.
    _universe.dockedFlag = 0u;
    ChangeView(_universe, _ports, 0u);
  }

  std::uint8_t ShowTitleShip(Universe& _universe, Ports& _ports, std::uint8_t _token, ShipType _shipType, std::uint8_t _distance) noexcept
  {
    // 6502: STY distaway / PHA / STX TYPE. The distance and the token are arguments here; `TYPE`
    // is a real byte and `NWSHP` below reads it back.
    _universe.flight.type = _shipType;

    /*
     * 6502: LDA #&FF / STA MULIE / JSR RESET / LDA #0 / STA MULIE.
     *
     * The bracket is what keeps the theme playing. `RESET` is reached from the title screen with
     * the music already started, and `stopbd` opens `BIT MULIE / BMI itsoff` -- so the flag is how
     * one caller of `RESET` gets a different sound from every other.
     */
    _universe.status.titleReset = 0xFFu;
    ResetGame(_universe, _ports);
    _universe.status.titleReset = 0u;

    _ports.start.ClearKeyLogger(); // 6502: JSR ZEKTRAN

    // 6502: LDA #32 / JSR DOVDU19 -- the title screen's palette on the Master, an RTS here.

    SetUpScreen(_universe, _ports, TITLE_CLEAR_VIEW); // 6502: LDA #13 / JSR TT66
    _universe.view = 0u;                              // 6502: LDA #0 / STA QQ11

    /*
     * 6502: LDA #96 / STA INWK+14 / LDA #96 / STA INWK+7 / LDX #127 / STX INWK+29 / STX INWK+30.
     *
     * `INWK+14` is the nose vector's z high byte, which `RES2`'s `ZINF` has just set to 96 WITH the
     * sign bit; writing 96 again clears that bit, so the ship faces away from the player rather
     * than towards them. The two 127s are the roll and pitch counters at maximum, and they are the
     * whole of why it turns: `MVEIT` steps the orientation by them on every frame.
     */
    _universe.work.nose.z.hi = TITLE_START_DISTANCE;
    _universe.work.z.hi = TITLE_START_DISTANCE;
    _universe.work.rollCounter = TITLE_SPIN;
    _universe.work.pitchCounter = TITLE_SPIN;

    // 6502: INX / STX QQ17 -- 128, which is sentence case, and it is what the prompt prints in.
    _universe.text.caseFlags = 0x80u;

    // 6502: LDA TYPE / JSR NWSHP. The slot is kept because `LL9` needs the ship's block in `K%` as
    // well as the copy in `INWK` -- part 1 writes two bytes straight through `INF`.
    const NewShip created = AddShip(_universe.bubble, _universe.work, _shipType, _universe.flight.blueprint);
    const std::uint8_t slot = created.created ? created.slot : std::uint8_t{0};

    _universe.text.column = 6u;                            // 6502: LDA #6 / JSR DOXC
    PrintThenNewline(_ports.printer, TITLE_HEADING_TOKEN); // 6502: LDA #30 / JSR plf
    _ports.sink.Put(10u);                                  // 6502: LDA #10 / JSR TT26
    _universe.text.column = 6u;                            // 6502: LDA #6 / JSR DOXC

    // 6502: LDA PATG / BEQ awe / LDA #13 / JSR DETOK -- the credits, and the byte that shows them
    // also changes what the main game loop spawns.
    if (_universe.options.authorNames != 0u)
    {
      _ports.tokens.Print(TITLE_AUTHORS_TOKEN);
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
    _universe.flight.delta = 0u;
    _universe.options.joystick = 0u;

    _universe.text.row = TITLE_PROMPT_ROW;     // 6502: LDA #15 / STA YC
    _universe.text.column = TITLE_PROMPT_LEFT; // 6502: LDA #1 / STA XC
    _ports.tokens.Print(_token);               // 6502: PLA / JSR DETOK -- the caller's own token

    _universe.text.column = 3u;              // 6502: LDA #3 / JSR DOXC
    _ports.tokens.Print(TITLE_BYLINE_TOKEN); // 6502: LDA #12 / JSR DETOK

    _universe.flight.steerCone = TITLE_CNT2;       // 6502: LDA #12 / STA CNT2
    _universe.flight.mainLoopCounter = TITLE_MCNT; // 6502: LDA #5 / STA MCNT
    _universe.options.joystick = 0xFFu;            // 6502: LDA #&FF / STA JSTK

    for (;;)
    {
      // 6502: .TLL2 LDA INWK+7 / CMP #1 / BEQ TL1 / DEC INWK+7 -- the ship closes and then holds.
      if (_universe.work.z.hi != 1u)
      {
        _universe.work.z.hi = static_cast<std::uint8_t>(_universe.work.z.hi - 1u);
      }

      /*
       * 6502: .TL1 JSR MVEIT.
       *
       * The answer is discarded and saying so is the point: `MVEIT` only reaches `TACTICS` for a
       * ship whose `INWK+32` has bit 7 set, and `TITLE` builds its ship with `ZINF` and then sets
       * byte 32 to nothing. So the AI cannot run here and cannot kill anybody, and there is no
       * player to kill -- the title screen has no energy banks (§6.122).
       */
      (void)MoveShip(_universe, _ports);

      /*
       * 6502: LDX distaway / STX INWK+6 / LDA MCNT / AND #3 / LDA #0 / STA INWK / STA INWK+3.
       *
       * Three stores that undo what `MVEIT` just did to the position, so the ship turns on the
       * spot: the z low byte goes back to the caller's distance and both coordinate low bytes to
       * zero. The `LDA MCNT / AND #3` between them is DEAD -- `LDA #0` overwrites the accumulator
       * before anything can read it.
       */
      _universe.work.z.lo = _distance;
      _universe.work.x.lo = 0u;
      _universe.work.y.lo = 0u;

      // 6502: JSR LL9 -- the title's ship is never killed, so the carry it is reached with goes unread.
      DrawShip(_universe.canvas, _universe.geometry, _universe.math, _universe.clip, _universe.projection, _universe.work,
               _universe.bubble.blocks[slot], _universe.heap, *_universe.flight.blueprint, _universe.flight.type, _ports.drawing,
               _universe.rng, false);

      // 6502: JSR RDKEY / DEC MCNT.
      _ports.present.HoldTitleFrame(_universe.work.z.hi); // 6502: TLL2's pace
      const TitleKey scan = ScanKeyboard(_universe.keys, _universe.video, _universe.memoryMap, _universe.view, _ports.keyboard);
      _universe.flight.mainLoopCounter = static_cast<std::uint8_t>(_universe.flight.mainLoopCounter - 1u);

      /*
       * 6502: BIT KY7 / BMI TL3 / BCC TLL2 / INC JSTK.
       *
       * Fire returns with `JSTK` still &FF; any other key runs the `INC` first and leaves it zero.
       * The prompt reads as a choice of two equal ways to continue and is actually the joystick
       * question.
       */
      if ((_universe.keys[KEY_FIRE] & 0x80u) != 0u)
      {
        return scan.key;
      }
      if (scan.pressed)
      {
        _universe.options.joystick = static_cast<std::uint8_t>(_universe.options.joystick + 1u);
        return scan.key;
      }
    }
  }

  void PrepareDeathScene(Universe& _universe, Ports& _ports) noexcept
  {
    // 6502: JSR EXNO3 -- `LDY #sfxexpl / BNE NOISE`, and the carry is whatever killed us.
    (void)PlaySoundEffect(_universe.sound, SoundEffect::Explosion, false);

    ResetShipAndBubble(_universe, _ports); // 6502: JSR RES2

    // 6502: ASL DELTA / ASL DELTA -- and the upstream comment says "divide by 4", which is the
    // BBC's `LSR`. This build SHIFTS LEFT twice, so the speed is multiplied (§6.117).
    _universe.flight.delta = static_cast<std::uint8_t>(_universe.flight.delta << 2);

    /*
     * 6502: LDX #24 / JSR DET1 / JSR TT66.
     *
     * `DET1` is a bare `RTS` here, so the `LDX #24` goes nowhere and A is NOT set to 6 -- what
     * `TT66` gets is whatever `RES2` left in it (§6.117). `DEATH_VIEW` is that byte, measured.
     */
    SetUpScreen(_universe, _ports, DEATH_VIEW);

    // 6502: JSR BOX -- the SAME border again, and `BOX2` EORs, so drawing it twice rubs it out.
    DrawFullBorder(_universe.canvas);

    // 6502: LDA #0 / STA SCBASE+&1F1F / STA SCBASE+&118 -- the two bytes `BOX` STORES instead of
    // EORing, which a second pass therefore cannot remove.
    _universe.canvas.Write(BOTTOM_RIGHT_CORNER, 0u);
    _universe.canvas.Write(BORDER_TOP_RIGHT, 0u);

    // 6502: JSR nWq -- a whole new stardust field over the cleared screen.
    SeedStardustField(_universe.canvas, _universe.dust, _universe.rng, false);

    // 6502: LDA #12 / JSR DOYC / JSR DOXC -- the cursor, then the sign.
    _universe.text.row = GAME_OVER_ROW;
    _universe.text.column = GAME_OVER_COLUMN;
    _ports.printer.PrintPhrase(GAME_OVER_TOKEN); // 6502: LDA #146 / JSR ex

    /*
     * 6502: .D1 -- spawn wreckage until the fifth slot is taken.
     *
     * `LDA FRIN+4 / BEQ D1` is the condition, so this fills slots 0 to 4 -- five pieces, not four,
     * and the loop tests the slot AFTER creating one.
     */
    /*
     * The carry `Ze` rotates into its first `DORND`. On the first pass it is what `ex` left, and
     * that is always CLEAR: every character goes out through `CHPR`, which ends `CLC / RTS`, and
     * nothing on the way back from the last one to `.D1` touches the flag. On every later pass it
     * is what the `JSR DORND` at the bottom of the loop left in its last `ADC`, because `AND`,
     * `LDY`, `STA (INF),Y`, `LDA` and `BEQ` leave it alone (§6.117).
     */
    bool carry = false;

    do
    {
      const RngResult roll = SeedDebris(_universe.work, _universe.rng, carry); // 6502: JSR Ze

      _universe.work.x.lo = static_cast<std::uint8_t>(roll.value >> 2); // 6502: LSR A / LSR A / STA INWK

      // 6502: LDY #0 / STY QQ11 / STY INWK+1 / STY INWK+4 / STY INWK+7 / STY INWK+32.
      _universe.view = 0u;
      _universe.work.x.hi = 0u;
      _universe.work.y.hi = 0u;
      _universe.work.z.hi = 0u;
      _universe.work.ai = 0u;

      // 6502: DEY / STY MCNT -- 255, so every timer-based call in the loop is stopped.
      _universe.flight.mainLoopCounter = 0xFFu;

      // 6502: EOR #%00101010 / STA INWK+3 / ORA #%01010000 / STA INWK+6.
      const std::uint8_t flipped = static_cast<std::uint8_t>(_universe.work.x.lo ^ 0x2Au);
      _universe.work.y.lo = flipped;
      _universe.work.z.lo = static_cast<std::uint8_t>(flipped | 0x50u);

      // 6502: TXA / AND #%10001111 / STA INWK+29 -- a gentle roll, sign kept.
      _universe.work.rollCounter = static_cast<std::uint8_t>(roll.previous & 0x8Fu);

      _universe.status.laserCount = DEATH_FRAMES; // 6502: LDY #64 / STY LASCT

      // 6502: SEC / ROR A / AND #%10000111 / STA INWK+30 -- and the `A` is the roll byte above,
      // not the random one: `TXA` left it there.
      const std::uint8_t pitched = static_cast<std::uint8_t>((_universe.work.rollCounter >> 1) | 0x80u);
      _universe.work.pitchCounter = static_cast<std::uint8_t>(pitched & 0x87u);

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
      const ShipType type = plate ? ShipType::AlloyPlate : ShipType::Canister;

      // 6502: JSR fq1 -- and the carry it takes into its `ROL A` is the one `BCC D3` just tested.
      const NewShip made = AddDebris(_universe.bubble, _universe.work, type, _universe.flight.delta, plate, _universe.flight.blueprint);

      // 6502: JSR DORND / AND #%10000000 / LDY #31 / STA (INF),Y -- half the wreckage is already
      // dead, which is what makes some of it explode as it goes past. The carry it rotates in is
      // `NWSHP`'s answer: `SEC / RTS` for a ship made, `CLC / RTS` for one refused.
      const RngResult state = _universe.rng.Next(made.created);
      if (made.created)
      {
        _universe.bubble.blocks[made.slot].state = static_cast<std::uint8_t>(state.value & 0x80u);
      }
      carry = state.carry;
    } while (_universe.bubble.slots[DEATH_DEBRIS_SLOT] == 0u);
  }

  void Die(Universe& _universe, Ports& _ports) noexcept
  {
    PrepareDeathScene(_universe, _ports);

    ClearFlightKeys(_universe.keys); // 6502: JSR U%

    // 6502: STA DELTA -- and A is the zero `U%` left in it, so we stop dead.
    _universe.flight.delta = 0u;

    /*
     * 6502: JSR M% / JSR NOSPRITES / .D2 JSR M% / DEC LASCT / BNE D2.
     *
     * EVERY `M%` IS A FRAME, and each one is shown. The 6502 needed no instruction for that -- the
     * VIC-II was reading the bitmap the whole time, so a frame was on the screen for exactly as
     * long as the next took to compute. A port that draws sixty-five frames between two presents
     * reproduces the arithmetic and none of the sequence (§6.109's argument, and §6.149's bug).
     *
     * `FRIN` is walked for the ship count on every frame, because the cost of one depends on it and
     * the wreckage flying past empties the bubble -- so the rate rises through the sequence.
     */
    const auto hold = [&_universe, &_ports]()
    {
      std::uint8_t ships = 0;
      for (const std::uint8_t type : _universe.bubble.slots)
      {
        if (type == 0u)
        {
          break;
        }
        ++ships;
      }
      _ports.present.HoldFlightFrame(ships);
    };

    (void)MainFlightLoop(_universe, _ports);
    HideAllSprites(_universe.video, _universe.memoryMap);
    hold();

    do
    {
      (void)MainFlightLoop(_universe, _ports);
      _universe.status.laserCount = static_cast<std::uint8_t>(_universe.status.laserCount - 1u);

      hold();
    } while (_universe.status.laserCount != 0u);

    // 6502: LDX #31 / JSR DET1 / JMP DEATH2 -- the first is a bare RTS and the second is the
    // caller's own death exit, which `Main.cpp` already wires as `RES2` then `BR1` (§6.25).
  }

  void AbandonShip(Universe& _universe, Ports& _ports) noexcept
  {
    ResetShipAndBubble(_universe, _ports); // 6502: JSR RES2

    /*
     * 6502: LDX #CYL / STX TYPE / JSR FRS1 / BCS ES1 / LDX #CYL2 / JSR FRS1.
     *
     * `BCS` takes the SUCCESS, so the second call is the FAILURE path: the bubble had no room for a
     * Cobra and gets a pirate Cobra instead. Two different blueprints, and the one you get is
     * decided by how full the bubble was when you punched out.
     */
    _universe.flight.type = ShipType::CobraMk3;
    /*
     * `FRS1` takes `DELTA` and `MSTG` rather than a speed: `LDA DELTA / ROL A / STA INWK+27`, with
     * the carry `MSTG`'s bit 7 (§6.121). So the abandoned ship leaves at TWICE the speed you were
     * doing, plus one if no missile was locked -- and `RES2` has just set `DELTA` to 3, so it is
     * always 6 or 7 whatever you were doing when you punched out.
     */
    NewShip abandoned = SpawnShipAhead(_universe.bubble, _universe.work, ShipType::CobraMk3, _universe.flight.delta,
                                       _universe.bubble.missileTarget, _universe.flight.blueprint);
    if (!abandoned.created)
    {
      abandoned = SpawnShipAhead(_universe.bubble, _universe.work, ShipType::CobraMk3Pirate, _universe.flight.delta,
                                 _universe.bubble.missileTarget, _universe.flight.blueprint);
    }

    /*
     * 6502: .ES1 LDA #8 / STA INWK+27 / LDA #194 / STA INWK+30 / LSR A / STA INWK+32.
     *
     * 194 is the pitch and 97 is both the AI byte and the number of frames below, because the loop
     * counts down through `INWK+32` itself.
     */
    _universe.work.speed = ESCAPE_SPEED;
    _universe.work.pitchCounter = ESCAPE_PITCH;
    _universe.work.ai = static_cast<std::uint8_t>(ESCAPE_PITCH >> 1u);

    // 6502: .ESL1 JSR MVEIT / JSR LL9 / DEC INWK+32 / BNE ESL1 -- and the death path `MVEIT` can
    // reach is unreachable here, because the ship flying away is not shooting at anybody.
    while (_universe.work.ai != 0u)
    {
      static_cast<void>(MoveShip(_universe, _ports));
      /*
       * 6502: JSR LL9 -- and the SLOT it writes back to is the one `FRS1` just filled, through
       * `INF`. Handing it slot 0 would have `LL9` writing its bookkeeping into the PLANET, which
       * is what the port did until the oracle disagreed about the planet's speed byte.
       */
      DrawShip(_universe.canvas, _universe.geometry, _universe.math, _universe.clip, _universe.projection, _universe.work,
               _universe.bubble.blocks[abandoned.slot], _universe.heap, *_universe.flight.blueprint, _universe.flight.type, _ports.drawing,
               _universe.rng,
               false); // the pod is never killed, so the carry goes unread
      --_universe.work.ai;
    }

    // 6502: JSR SCAN -- and it is drawn ONCE, after the loop, so the blip the animation left
    // on the scanner is erased rather than added to. `SCAN` is an EOR.
    DrawScannerBlip(_universe.canvas, _universe.work, _universe.flight.type, _universe.view);

    // 6502: LDA #0 / LDX #16 / .ESL2 STA QQ20,X / DEX / BPL ESL2 -- SEVENTEEN bytes, because the
    // loop runs from 16 down THROUGH zero.
    for (std::size_t item = 0; item < MARKET_ITEM_COUNT; ++item)
    {
      _universe.commander.cargoHold[item] = 0u;
    }

    _universe.commander.legalStatus = 0u; // 6502: STA FIST -- a clean record
    _universe.commander.escapePod = 0u;   // 6502: STA ESCP -- and the pod is spent

    /*
     * 6502: LDA TRIBBLE / ORA TRIBBLE+1 / BEQ nosurviv / JSR DORND / AND #7 / ORA #1 / STA TRIBBLE
     * / LDA #0 / STA TRIBBLE+1.
     *
     * `ORA #1` is what stops the population reaching zero: one to eight survive, never none, so
     * abandoning ship never clears them. The high byte is zeroed, which is the whole of the mercy.
     */
    const std::uint8_t low = _universe.commander.tribbles.lo;
    const std::uint8_t high = _universe.commander.tribbles.hi;

    if (static_cast<std::uint8_t>(low | high) != 0u)
    {
      /*
       * 6502: JSR DORND -- and it rotates in a SET carry, which `SCAN` left. Nothing between the
       * two touches the flag: `LDA #0 / LDX #16`, the `.ESL2` store loop, `STA FIST`, `STA ESCP`,
       * `LDA TRIBBLE / ORA TRIBBLE+1 / BEQ` are all carry-blind. Measured with §6.118's instrument
       * rather than derived, because `SCAN` has several exits and the arithmetic near them is a
       * screen address rather than anything this routine can reason about.
       */
      const RngResult survivors = _universe.rng.Next(true);
      _universe.commander.tribbles.lo = static_cast<std::uint8_t>((survivors.value & 7u) | 1u);
      _universe.commander.tribbles.hi = 0u;
    }

    // 6502: .nosurviv LDA #70 / STA QQ14 / JMP GOIN -- seven light years, and the docking is the
    // caller's, the way every `JMP` out of a routine has been.
    _universe.commander.fuel = ESCAPE_FUEL;
  }

} // namespace Elite
