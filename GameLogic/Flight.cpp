#include "pch.h"

#include "Flight.h"

#include "Scanner.h"

#include "Combat.h"
#include "Lines2x.h"
#include "Market.h"
#include "Music.h"
#include "PlanetDraw.h"
#include "Spawn.h"

namespace Elite
{
  namespace
  {
    /// Twelve in this build, whatever three versions' comments say (§6.44).
    inline constexpr std::uint8_t STARDUST_COUNT = 12;

    /// `JSTY`'s centre -- the middle of a control's range, and the sign both rate bytes
    /// carry when the ship is not turning.
    inline constexpr std::uint8_t CONTROL_CENTRE = 128;

    /// The roll and the drift a launch starts with, from ONE load into three bytes.
    inline constexpr std::uint8_t LAUNCH_ROLL = 3;

    /// `Yx2M1`, the bottom row `CHKON` counts as on screen, which `TT23` moves to 199 and
    /// this puts back.
    inline constexpr std::uint8_t SPACE_VIEW_LAST_ROW = 143;

    /// The `QQ11` value `TT110` leaves behind, which is not a view at all: it is what makes
    /// `LOOK1` take its "the view is changing" branch rather than its "already there" one (§6.81's
    /// finding, from the other side).
    inline constexpr std::uint8_t VIEW_LAUNCHING = 255;
  } // namespace

  void ClearBubbleState(Universe& _universe, Ports& _ports) noexcept
  {
    // FRIN and MANY -- the slots and the per-type counts, which `SSPR` is part of (§6.58).
    for (std::uint8_t& slot : _universe.bubble.slots)
    {
      slot = 0u;
    }
    for (std::uint8_t& count : _universe.bubble.counts)
    {
      count = 0u;
    }

    _universe.bubble.junk = 0u;
    _universe.control.dockingComputer = 0u; // auto
    _universe.status.ecmOurs = 0u;
    _universe.status.midJump = 0u;
    _universe.status.cabinTemperature = 0u;
    _universe.status.viewLaser = 0u;
    _universe.status.missileArmed = 0u;
    _universe.spaceView = 0u;
    _universe.status.laserCount = 0u;
    _universe.status.laserTemperature = 0u;
    _universe.screen.hyperspaceEffect = 0u;
    _universe.explosions = 0u;
    _universe.message.delay = 0u;
    _universe.message.append = 0u;          // de
  }

  void ResetShipAndBubble(Universe& _universe, Ports& _ports) noexcept
  {
    // JSR stopbd
    StopDockingMusic(_universe.music, _universe.status.titleReset, _universe.sound, _universe.memoryMap, _ports.sid);

    /*
     * A bomb that is running is switched off, and then `BOMB` is stored to.
     *
     * The store writes what `BOMBOFF` left in the accumulator, which is the zero its own last
     * instruction loaded -- so the bomb is switched off by the routine and emptied by its
     * accumulator, and reading that store as "store the bomb" gets the value from the wrong
     * routine.
     */
    if ((_universe.commander.energyBomb & 0x80u) != 0u)
    {
      StopEnergyBomb(_universe.screen);
      _universe.commander.energyBomb = 0u;
    }

    _universe.dust.count = STARDUST_COUNT; // NOST into NOSTM

    // 255 into entry 0 of both halves of the ball heap, and into the missile lock.
    _universe.heaps.ball[0] = 0xFFu;
    _universe.heaps.ball[BALL_HEAP_SIZE] = 0xFFu;
    _universe.bubble.missileTarget = 0xFFu;

    /*
     * 128 into `JSTY` and the two sign bytes, then doubled to zero for the six after them.
     *
     * AND `JSTX` IS NOT HERE. The pitch rate is re-centred and the roll rate is not, and neither is
     * inside `ZERO`'s range -- so a launch inherits whatever roll the last flight ended on while
     * the pitch always starts straight.
     */
    _universe.control.pitch = CONTROL_CENTRE;
    _universe.flight.rollSign = CONTROL_CENTRE;
    _universe.flight.pitchSign = CONTROL_CENTRE;

    // 128 doubled is zero, which is where the next six stores get their value.
    _universe.flight.pitchRate = 0u;
    _universe.flight.pitchMagnitude = 0u;
    _universe.flight.rollSignFlipped = 0u;
    _universe.flight.pitchSignFlipped = 0u;
    _universe.flight.mainLoopCounter = 0u;
    _universe.trumbles.count = 0u;

    // One load into `DELTA`, `ALPHA` and `ALP1` -- three meanings.
    _universe.flight.speed = LAUNCH_ROLL;
    _universe.flight.rollRate = LAUNCH_ROLL;
    _universe.flight.rollMagnitude = LAUNCH_ROLL;

    _universe.text.palette = TEXT_COLOUR_WHITE; // &10 into COL2
    _universe.clip.clippingOff = 0u;                  // Zero into dontclip
    _universe.heaps.lowestVisibleRow = SPACE_VIEW_LAST_ROW;   // The bottom row into Yx2M1

    // The station bulb is a TOGGLE, so this puts it out only because `SSPR` says it was lit,
    // and the test is what keeps the two in step.
    if (_universe.bubble.Count(ShipType::Station) != 0u)
    {
      ToggleStationIndicator(_universe.canvas, &_universe.picture);
    }

    // An ECM still counting down is switched off first.
    if (_universe.status.ecmCountdown != 0u)
    {
      StopEcm(_universe.canvas, _universe.status, _universe.sound, &_universe.picture);
    }

    // Rub every ship off the screen and forget both line heaps.
    ClearAllShips(_universe.canvas, _universe.heaps, _universe.bubble, _universe.work, _universe.flight, _universe.view,
                  &_universe.picture);

    ClearBubbleState(_universe, _ports);

    // `SLSP` back to the top of the heap block -- the heap is empty again.
    _universe.bubble.heapBottom = HeapOffset::Top();

    ClearShip(_universe.work); // And no RTS -- it falls into ZINF
  }

  void ResetGame(Universe& _universe, Ports& _ports) noexcept
  {
    ClearBubbleState(_universe, _ports); // ZERO, which leaves A at zero for the loop below

    /*
     * A store loop from index 6 down to 0.
     *
     * Seven bytes from `BETA` upwards, and in THIS build that is the pitch pair, both hyperspace
     * counters, `ECMA` and the roll's two sign bytes -- not the text cursor the upstream comment
     * names, which is the BBC's layout at those addresses.
     */
    _universe.flight.pitchRate = 0u;
    _universe.flight.pitchMagnitude = 0u;
    _universe.status.hyperspaceCountdown = 0u;
    _universe.status.hyperspaceCounter = 0u;
    _universe.status.ecmCountdown = 0u;
    _universe.flight.rollMagnitude = 0u;
    _universe.flight.rollSign = 0u;

    /*
     * The index moved into `QQ12`, then three more bytes from `FSH` filled.
     *
     * X is 255 because the loop above ran off its end, and that 255 does TWO jobs: it is the flag
     * that says "docked", and it is the value the three shield and energy bytes are filled with.
     * The second only works because a full bank happens to be 255.
     */
    _universe.dockedFlag = 0xFFu;
    _universe.status.forwardShield = 0xFFu;
    _universe.status.aftShield = 0xFFu;
    _universe.status.energy = 0xFFu;

    ResetShipAndBubble(_universe, _ports); // And no RTS -- it falls into RES2
  }

  void DrawLaunchTunnel(Universe& _universe, Ports& _ports) noexcept
  {
    // The whoosh, and the carry `NOISE` returns is dropped because the next
    // instruction is a load. §6.99's third answer costs nothing here.
    (void)PlaySoundEffect(_universe.sound, SoundEffect::Missile, false);

    // The step is loaded here and `HFS2`'s first instruction is what stores it. This is the
    // only writer of the step on the launch path, and its absence is what §6.95 was working around.
    _universe.heaps.circleStep = LAUNCH_TUNNEL_STEP;

    /*
     * The view saved on the stack across the clear and restored after it.
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
    // HFS2 stores the step it was entered with -- the only writer on either tunnel's path,
    // which is the other half of §6.94's answer.
    _universe.heaps.circleStep = _step;

    /*
     * The view saved on the stack across the clear and restored after it.
     *
     * The screen is cleared to a space view and the view type is then PUT BACK to whatever the
     * caller had. So the tunnel is drawn on a blank space view while the game still believes it is
     * showing the docked screen -- which is exactly right, because the caller has not finished
     * leaving it yet.
     */
    const std::uint8_t saved = _universe.view;
    SetUpScreen(_universe, _ports, 0u);
    _universe.view = saved;

    // Falls into HFS1.
    DrawHyperspaceRings(_universe.canvas, _universe.heaps, _universe.geometry, _universe.math, _universe.clip, _ports.present,
                        &_universe.picture);
  }

  void DrawHyperspaceTunnel(Universe& _universe, Ports& _ports) noexcept
  {
    /*
     * The first hyperspace effect through `NOISE2` with a sustain and a
     * frequency of its own, then `sfxwhosh` through `NOISE`, then one frame of `DELAY`, then
     * `sfxhyp1 + 128`.
     *
     * The last one is `NOISE`'s LAYERING entry: bit 7 of the effect number means "do not check
     * whether it is already playing", so the second hyperspace sound stacks on the first rather
     * than replacing it. That bit is the argument, not a separate routine.
     */
    /*
     * The carry into `NOISE2` here is the CALLER's -- `LDY`, `LDA` and `LDX` touch no flag -- and
     * it is unobservable, which is why false is passed rather than threaded through `LL164`.
     * `NOISE` reads it on one path only -- the test of `DNOIZ`, the sound-off switch -- where it
     * becomes the RETURN value; nothing it writes depends on it, and `HYPNOISE` discards the
     * answer.
     */
    (void)PlaySoundEffectPitched(_universe.sound, SoundEffect::Hyperspace, HYPERSPACE_SUSTAIN, HYPERSPACE_FREQUENCY, false);
    (void)PlaySoundEffect(_universe.sound, SoundEffect::Missile, false);

    // One vertical sync of `DELAY`, which is what the pacing object holds for.
    _ports.present.Present();

    (void)PlaySoundEffect(_universe.sound, SoundEffect::HyperspaceAgain, false);

    // Into HFS2 with a step of 4, and straight back out.
    DrawTunnel(_universe, _ports, HYPERSPACE_TUNNEL_STEP);
  }

  void Launch(Universe& _universe, Ports& _ports, std::uint8_t _crosshairX, std::uint8_t _crosshairY, SystemSeeds& _selected) noexcept
  {
    // A zero `QQ12` skips to NLUNCH -- pressing "1" in flight only changes the view.
    if (_universe.dockedFlag != 0u)
    {
      // LAUN, over the docked screen it is still showing.
      DrawLaunchTunnel(_universe, _ports);
      ResetShipAndBubble(_universe, _ports);

      /*
       * For the SEEDS, not for the distance. The planet's look comes from the
       * system's own seeds through `tek`, so a launch has to know which system it is leaving.
       */
      const NearestSystem found = FindNearestSystem(_universe.commander.galaxySeeds, _crosshairX, _crosshairY, _universe.commander.systemX,
                                                    _universe.commander.systemY);
      _selected = found.seeds;

      /*
       * The planet spawned, then the z sign and high byte moved and the station spawned.
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
      (void)AddStation(_universe, _ports);

      _universe.flight.speed = LAUNCH_SPEED; // 12 into DELTA

      // The contraband penalty is ORed into `FIST` -- the fine is levied by leaving, not by
      // being scanned.
      _universe.commander.legalStatus = static_cast<std::uint8_t>(ContrabandPenalty(_universe.commander) | _universe.commander.legalStatus);

      _universe.view = VIEW_LAUNCHING; // 255 into QQ11

      /*
       * Eight rings over the screen the tunnel left, and they erase themselves
       * because `LOOK1` below clears the screen anyway (§6.94).
       *
       * `STP` is still the 8 `LAUN` stored, which is the second half of §6.94's answer: the step
       * IS written on this path, by the routine the port had left as a stub (§6.109).
       */
      DrawHyperspaceRings(_universe.canvas, _universe.heaps, _universe.geometry, _universe.math, _universe.clip, _ports.present,
                        &_universe.picture);
    }

    // NLUNCH -- and the zero that clears `QQ12` is the same register the view change is
    // given, so a launch always ends looking forwards.
    _universe.dockedFlag = 0u;
    ChangeView(_universe, _ports, 0u);
  }

  std::uint8_t ShowTitleShip(Universe& _universe, Ports& _ports, std::uint8_t _token, ShipType _shipType, std::uint8_t _distance) noexcept
  {
    // The distance and the token are arguments here, stored and stacked; `TYPE` is a real
    // byte and `NWSHP` below reads it back.
    _universe.flight.type = _shipType;

    /*
     * `MULIE` set, `RESET` called, `MULIE` cleared.
     *
     * The bracket is what keeps the theme playing. `RESET` is reached from the title screen with
     * the music already started, and `stopbd` opens by testing `MULIE` and leaving if its top bit
     * is set -- so the flag is how one caller of `RESET` gets a different sound from every other.
     */
    _universe.status.titleReset = 0xFFu;
    ResetGame(_universe, _ports);
    _universe.status.titleReset = 0u;

    _universe.keys.fill(0u);

    // DOVDU19 with 32 -- the title screen's palette on the Master, a bare return here.

    SetUpScreen(_universe, _ports, TITLE_CLEAR_VIEW);
    _universe.view = 0u;                              // Zero into QQ11

    /*
     * 96 into two bytes of the block, then 127 into the two counters.
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

    // The 127 above stepped once and stored into QQ17 -- 128, which is sentence case, and
    // it is what the prompt prints in.
    _universe.text.caseFlags = 0x80u;

    // NWSHP, on the type stored above. The slot is kept because `LL9` needs the ship's block
    // in `K%` as well as the copy in `INWK` -- part 1 writes two bytes straight through `INF`.
    const NewShip created = AddShip(_universe.bubble, _universe.work, _shipType, _universe.flight.blueprint);
    const std::uint8_t slot = created.created ? created.slot : std::uint8_t{0};

    _universe.text.column = 6u;
    PrintThenNewline(_ports.printer, TITLE_HEADING_TOKEN); // Plf with 30
    _ports.sink.Put(10u);
    _universe.text.column = 6u;

    // `PATG` gates it -- the credits, and the byte that shows them also changes what the
    // main game loop spawns.
    if (_universe.options.authorNames != 0u)
    {
      _ports.tokens.Print(TITLE_AUTHORS_TOKEN);
    }

    /*
     * A zero `brkd` branches straight to BRBR2 -- and the branch it guards is REPLACED
     * rather than omitted.
     *
     * `brkd` counts BRKs and the message it prints is read through `(&FD),Y`, a pointer the BRK
     * handler leaves behind. That handler is row 32's -- the C64's NMI vectors and Kernal setup,
     * which the ledger marks Replace and this port has no equivalent for -- so `brkd` is zero for
     * the life of the process and the branch is not reachable rather than not written.
     */

    // Zero into both.
    _universe.flight.speed = 0u;
    _universe.options.joystick = 0u;

    _universe.text.row = TITLE_PROMPT_ROW;     // 15 into YC
    _universe.text.column = TITLE_PROMPT_LEFT; // 1 into XC
    _ports.tokens.Print(_token);               // DETOK on the token pulled back off the stack -- the caller's own

    _universe.text.column = 3u;
    _ports.tokens.Print(TITLE_BYLINE_TOKEN);

    _universe.flight.steerCone = TITLE_CNT2;       // 12 into CNT2
    _universe.flight.mainLoopCounter = TITLE_MCNT; // 5 into MCNT
    _universe.options.joystick = 0xFFu;            // &FF into JSTK

    for (;;)
    {
      // The z high byte counts down to 1 and stops: the ship closes, then holds.
      if (_universe.work.z.hi != 1u)
      {
        _universe.work.z.hi = static_cast<std::uint8_t>(_universe.work.z.hi - 1u);
      }

      /*
       * MVEIT, and nothing else.
       *
       * The answer is discarded and saying so is the point: `MVEIT` only reaches `TACTICS` for a
       * ship whose `INWK+32` has bit 7 set, and `TITLE` builds its ship with `ZINF` and then sets
       * byte 32 to nothing. So the AI cannot run here and cannot kill anybody, and there is no
       * player to kill -- the title screen has no energy banks (§6.122).
       */
      (void)MoveShip(_universe, _ports);

      /*
       * Three stores after the move, with a dead load between two of them.
       *
       * Three stores that undo what `MVEIT` just did to the position, so the ship turns on the
       * spot: the z low byte goes back to the caller's distance and both coordinate low bytes to
       * zero. The masked read of `MCNT` between them is DEAD -- the zero load that follows
       * overwrites the accumulator before anything can read it.
       */
      _universe.work.z.lo = _distance;
      _universe.work.x.lo = 0u;
      _universe.work.y.lo = 0u;

      // The title's ship is never killed, so the carry it is reached with goes unread.
      DrawShip(_universe, _universe.bubble.blocks[slot], false);

      // RDKEY, and then the counter steps down.
      _ports.present.HoldTitleFrame(_universe.work.z.hi); // TLL2's pace
      const TitleKey scan = ScanKeyboard(_universe.keys, _universe.video, _universe.memoryMap, _universe.view, _ports.keyboard);
      _universe.flight.mainLoopCounter = static_cast<std::uint8_t>(_universe.flight.mainLoopCounter - 1u);

      /*
       * The fire key tested first, and the loop's two other exits after it.
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
    // The explosion effect through `NOISE`, and the carry is whatever killed us.
    (void)PlaySoundEffect(_universe.sound, SoundEffect::Explosion, false);

    ResetShipAndBubble(_universe, _ports);

    // The speed is shifted LEFT twice, and the upstream comment says "divide by 4", which
    // is the BBC's shift the other way. This build multiplies (§6.117).
    _universe.flight.speed = static_cast<std::uint8_t>(_universe.flight.speed << 2);

    /*
     *
     * `DET1` is a bare return here, so the 24 goes nowhere and A is NOT set to 6 -- what
     * `TT66` gets is whatever `RES2` left in it (§6.117). `DEATH_VIEW` is that byte, measured.
     */
    SetUpScreen(_universe, _ports, DEATH_VIEW);

    // The SAME border again, and `BOX2` EORs, so drawing it twice rubs it out.
    DrawFullBorder(_universe.canvas, &_universe.picture);

    // Two screen bytes zeroed -- the two `BOX` STORES instead of
    // EORing, which a second pass therefore cannot remove.
    _universe.canvas.Write(BOTTOM_RIGHT_CORNER, 0u);
    _universe.canvas.Write(BORDER_TOP_RIGHT, 0u);
    // Guarded explicitly: these two twins take the surface by REFERENCE, so there is no null for
    // `DrawingTwins` to test and section 8.4's switch has to be read here.
    if (_universe.picture.Drawing())
    {
      WriteBitmapByte2x(_universe.picture, BOTTOM_RIGHT_CORNER, 0u, false);
      WriteBitmapByte2x(_universe.picture, BORDER_TOP_RIGHT, 0u, false);
    }

    // A whole new stardust field over the cleared screen.
    SeedStardustField(_universe.canvas, _universe.dust, _universe.rng, false, &_universe.picture);

    // DOYC then DOXC, both with 12 -- the cursor, then the sign.
    _universe.text.row = GAME_OVER_ROW;
    _universe.text.column = GAME_OVER_COLUMN;
    _ports.printer.PrintPhrase(GAME_OVER_TOKEN); // Ex with 146

    /*
     * .D1 -- spawn wreckage until the fifth slot is taken.
     *
     * An empty fifth slot is the condition, so this fills slots 0 to 4 -- five pieces, not four,
     * and the loop tests the slot AFTER creating one.
     */
    /*
     * The carry `Ze` rotates into its first `DORND`. On the first pass it is what `ex` left, and
     * that is always CLEAR: every character goes out through `CHPR`, which returns with the carry
     * clear, and nothing on the way back from the last one to `.D1` touches the flag. On every
     * later pass it is what the generator at the bottom of the loop left in its last addition,
     * because the mask, the index, the store, the load and the branch after it are all carry-blind
     * (§6.117).
     */
    bool carry = false;

    do
    {
      const RngResult roll = SeedDebris(_universe.work, _universe.rng, carry);

      _universe.work.x.lo = static_cast<std::uint8_t>(roll.value >> 2); // Two shifts right, then stored into INWK

      // Zero into the view and four bytes of the block.
      _universe.view = 0u;
      _universe.work.x.hi = 0u;
      _universe.work.y.hi = 0u;
      _universe.work.z.hi = 0u;
      _universe.work.ai = 0u;

      // That zero stepped down into MCNT -- 255, so every timer-based call in the loop stops.
      _universe.flight.mainLoopCounter = 0xFFu;

      // The byte folded twice, into the y low byte and then the z low byte.
      const std::uint8_t flipped = static_cast<std::uint8_t>(_universe.work.x.lo ^ 0x2Au);
      _universe.work.y.lo = flipped;
      _universe.work.z.lo = static_cast<std::uint8_t>(flipped | 0x50u);

      // The random X masked into the roll counter -- a gentle roll, sign kept.
      _universe.work.rollCounter = static_cast<std::uint8_t>(roll.previous & 0x8Fu);

      _universe.status.laserCount = DEATH_FRAMES; // 64 into LASCT

      // A set carry rotated in from the top, masked, into the pitch counter -- and what is
      // rotated is the ROLL byte above and not the random one, because the move to A left it there.
      const std::uint8_t pitched = static_cast<std::uint8_t>((_universe.work.rollCounter >> 1) | 0x80u);
      _universe.work.pitchCounter = static_cast<std::uint8_t>(pitched & 0x87u);

      /*
       * A byte read out of the blueprint table decides plate or canister.
       *
       * The load has no brackets, so it reads the byte AT `XX21 + 7` rather than through it --
       * always &D0, never zero, so the `BEQ` is dead and only the carry decides.
       *
       * AND THAT CARRY IS NOT `Ze`'S, whatever the upstream comment says ("which will be random
       * following the above call to Ze"). A set carry rotated into A sits between them, four
       * instructions up: the rotate shifts A right and puts A's OLD BIT 0 into the carry, and A
       * there is the roll
       * counter, `X AND %10001111`. So the wreckage is a plate when the random X was odd, which is
       * random but is a different random number from the one the comment names (§6.117).
       */
      const bool plate = (roll.previous & 1u) != 0u;
      const ShipType type = plate ? ShipType::AlloyPlate : ShipType::Canister;

      // fq1 -- and the carry it rotates in is the one the branch above just tested.
      const NewShip made = AddDebris(_universe.bubble, _universe.work, type, _universe.flight.speed, plate, _universe.flight.blueprint);

      // A random top bit into the slot's state byte -- half the wreckage is already dead,
      // which is what makes some of it explode as it goes past. The carry the generator rotates in
      // is `NWSHP`'s answer: set for a ship made, clear for one refused.
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

    ClearFlightKeys(_universe.keys);

    // Into DELTA -- and A is the zero `U%` left in it, so we stop dead.
    _universe.flight.speed = 0u;

    /*
     * One frame, the sprites hidden, then D2 -- a frame a turn until `LASCT` runs out.
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

    // DET1 with 31, then DEATH2 -- the first is a bare return and the second is the
    // caller's own death exit, which `Main.cpp` already wires as `RES2` then `BR1` (§6.25).
  }

  void AbandonShip(Universe& _universe, Ports& _ports) noexcept
  {
    ResetShipAndBubble(_universe, _ports);

    /*
     * FRS1 on a Cobra, and on the pirate blueprint if the first refused.
     *
     * `BCS` takes the SUCCESS, so the second call is the FAILURE path: the bubble had no room for a
     * Cobra and gets a pirate Cobra instead. Two different blueprints, and the one you get is
     * decided by how full the bubble was when you punched out.
     */
    _universe.flight.type = ShipType::CobraMk3;
    /*
     * `FRS1` takes `DELTA` and `MSTG` rather than a speed: the speed is rotated LEFT into the
     * ship's own speed byte, with the carry `MSTG`'s bit 7 (§6.121). So the abandoned ship leaves
     * at TWICE the speed you were doing, plus one if no missile was locked -- and `RES2` has just
     * set `DELTA` to 3, so it is always 6 or 7 whatever you were doing when you punched out.
     */
    NewShip abandoned = SpawnShipAhead(_universe.bubble, _universe.work, ShipType::CobraMk3, _universe.flight.speed,
                                       _universe.bubble.missileTarget, _universe.flight.blueprint);
    if (!abandoned.created)
    {
      abandoned = SpawnShipAhead(_universe.bubble, _universe.work, ShipType::CobraMk3Pirate, _universe.flight.speed,
                                 _universe.bubble.missileTarget, _universe.flight.blueprint);
    }

    /*
     * 8 into the speed, 194 into the pitch, and half of that into the AI byte.
     *
     * 194 is the pitch and 97 is both the AI byte and the number of frames below, because the loop
     * counts down through `INWK+32` itself.
     */
    _universe.work.speed = ESCAPE_SPEED;
    _universe.work.pitchCounter = ESCAPE_PITCH;
    _universe.work.ai = static_cast<std::uint8_t>(ESCAPE_PITCH >> 1u);

    // Move, draw, step the counter down, round again. The death path `MVEIT` can
    // reach is unreachable here, because the ship flying away is not shooting at anybody.
    while (_universe.work.ai != 0u)
    {
      static_cast<void>(MoveShip(_universe, _ports));
      /*
       * LL9 -- and the SLOT it writes back to is the one `FRS1` just filled, through
       * `INF`. Handing it slot 0 would have `LL9` writing its bookkeeping into the PLANET, which
       * is what the port did until the oracle disagreed about the planet's speed byte.
       */
      DrawShip(_universe, _universe.bubble.blocks[abandoned.slot],
               false); // the pod is never killed, so the carry goes unread
      --_universe.work.ai;
    }

    // SCAN -- and it is drawn ONCE, after the loop, so the blip the animation left
    // on the scanner is erased rather than added to. `SCAN` is an EOR.
    DrawScannerBlip(_universe.canvas, _universe.work, _universe.flight.type, _universe.view, &_universe.picture);

    // The cargo hold zeroed, SEVENTEEN bytes, because the loop runs from 16 down
    // THROUGH zero.
    for (std::size_t item = 0; item < MARKET_ITEM_COUNT; ++item)
    {
      _universe.commander.cargoHold[item] = 0u;
    }

    _universe.commander.legalStatus = 0u; // A clean record
    _universe.commander.escapePod = 0u;   // ESCP -- and the pod is spent

    /*
     * A population of zero skips to nosurviv; otherwise three random bits with the bottom
     * one forced go back into the low byte, and the high byte is zeroed.
     *
     * Forcing that bottom bit is what stops the population reaching zero: one to eight survive,
     * never none, so abandoning ship never clears them. The zeroed high byte is the whole of the
     * mercy.
     */
    const std::uint8_t low = _universe.commander.tribbles.lo;
    const std::uint8_t high = _universe.commander.tribbles.hi;

    if (static_cast<std::uint8_t>(low | high) != 0u)
    {
      /*
       * The generator rotates in a SET carry, which `SCAN` left. Nothing between the two
       * touches the flag: the cargo-hold clear, the two record bytes and the population test are
       * all carry-blind. Measured with §6.118's instrument
       * rather than derived, because `SCAN` has several exits and the arithmetic near them is a
       * screen address rather than anything this routine can reason about.
       */
      const RngResult survivors = _universe.rng.Next(true);
      _universe.commander.tribbles.lo = static_cast<std::uint8_t>((survivors.value & 7u) | 1u);
      _universe.commander.tribbles.hi = 0u;
    }

    // 70 into `QQ14`, seven light years, and the docking is the caller's, the
    // way every jump out of a routine has been.
    _universe.commander.fuel = FULL_TANK;
  }

} // namespace Elite
