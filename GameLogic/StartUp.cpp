#include "pch.h"

#include "StartUp.h"

#include "Commander.h"
#include "Dashboard.h"
#include "Flight.h"
#include "Music.h"
#include "Ports.h"
#include "Universe.h"

/*
 * Starting a game, and going back to the docking bay (slice 2e).
 */

namespace Elite
{

  void CrosshairsToCurrentSystem(const Commander& _commander, std::uint8_t& _crosshairX, std::uint8_t& _crosshairY) noexcept
  {
    // 6502: ping -- and QQ0 is TP+1, so this reads the commander block itself.
    _crosshairX = _commander.systemX;
    _crosshairY = _commander.systemY;
  }

  void CurrentSystemToCrosshairs(Commander& _commander, std::uint8_t _crosshairX, std::uint8_t _crosshairY) noexcept
  {
    // 6502: jmp -- two separate loads, not a loop, and it falls into `hy5`'s RTS.
    _commander.systemX = _crosshairX;
    _commander.systemY = _crosshairY;
  }

  ForcedKey ForceKey(std::uint8_t _key, std::uint8_t _dockedFlag, std::uint8_t _view, std::uint8_t _countdown,
                     bool _hyperspaceHeld) noexcept
  {
    ForcedKey result{};

    // 6502: JSR TT102 -- a real call, so the dispatch happens before the loop is chosen.
    result.outcome = ActionForKey(_key, _dockedFlag, _view, _countdown, _hyperspaceHeld);

    // 6502: LDA QQ12 / BEQ P%+5 / JMP MLOOP / JMP TT100. The BEQ skips the first jump.
    result.loop = (_dockedFlag != 0u) ? MainLoop::Docked : MainLoop::InSpace;
    return result;
  }

  ForcedKey EnterDockingBay(std::uint8_t& _dockedFlag, std::uint8_t _view, std::uint8_t _countdown, bool _hyperspaceHeld) noexcept
  {
    // 6502: LDA #&FF / STA QQ12 -- and &FF rather than 1, which is what TT102's BIT/BPL needs.
    _dockedFlag = 0xFF;

    // 6502: LDA #f8 / JMP FRCE -- the status key, pressed by the game on the player's behalf.
    return ForceKey(KEY_STATUS, _dockedFlag, _view, _countdown, _hyperspaceHeld);
  }

  ForcedKey StartGame(Universe& _universe, Ports& _ports, bool _hyperspaceHeld) noexcept
  {
    // 6502: JSR ZEKTRAN -- the key logger, before anything can be typed at it.
    _ports.start.ClearKeyLogger();

    // 6502: LDA #3 / JSR DOXC.
    _universe.text.column = TITLE_PROMPT_COLUMN;

    // 6502: JSR startat.
    StartTheme(_universe.music, _ports.sid);

    // 6502: LDX #CYL / LDA #6 / LDY #210 / JSR TITLE -- a Cobra Mk III, a long way off.
    const std::uint8_t answer = _ports.start.ShowTitleScreen(TITLE_LOAD_TOKEN, ShipType::CobraMk3, TITLE_COBRA_DISTANCE);

    /*
     * 6502: CMP #YINT / BNE QU5.
     *
     * ONLY "Y" opens the menu. Every other key -- including "N", including a joystick fire -- falls
     * straight through to the shared tail, so there is no way to answer this question wrongly.
     */
    if (answer == KEY_YES_INTERNAL)
    {
      // 6502: JSR stopat / JSR DFAULT / JSR SVE / JSR startat.
      StopMusic(_universe.music, _universe.sound, _ports.sid);

      // 6502: JSR DFAULT -- so the menu has a commander to print a name for.
      (void)LoadCommander(_universe.commanderFile, _universe.commander, _universe.commanderName);

      (void)DiskAccessMenu(_universe, _ports);

      /*
       * 6502: JSR startat -- and the theme restarts from the beginning.
       *
       * BR1 ignores SVE's carry, which every other caller reads. TT102 uses it to decide between
       * restarting the game and returning to the bay; here both answers lead to the same next
       * instruction, because `QU5`'s DFAULT below installs whatever the menu left in the image.
       */
      StartTheme(_universe.music, _ports.sid);
    }

    /*
     * 6502: QU5 -- JSR DFAULT, and this label is reached from three places: the "not Y" branch
     * above, the fall-through from the disk menu, and TT102's `JMP QU5` when a load succeeded.
     */
    (void)LoadCommander(_universe.commanderFile, _universe.commander, _universe.commanderName);

    // 6502: JSR msblob.
    ResetMissileIndicators(_universe.canvas, _universe.commander.missiles);

    // 6502: LDA #7 / LDX #ADA / LDY #48 / JSR TITLE -- an Adder, close up. Its key is discarded.
    (void)_ports.start.ShowTitleScreen(TITLE_START_TOKEN, ShipType::Adder, TITLE_ADDER_DISTANCE);

    // 6502: JSR stopat -- the only stop both paths reach.
    StopMusic(_universe.music, _universe.sound, _ports.sid);

    /*
     * 6502: JSR ping / JSR TT111 / JSR jmp -- and this SNAPS the commander's position.
     *
     * The crosshairs go to where the commander says it is, the search finds the nearest system that
     * the generator actually produces and writes its coordinates back over the crosshairs, and then
     * they are copied into the commander. A file whose coordinates fall between two systems starts
     * the game at whichever one was nearest.
     */
    CrosshairsToCurrentSystem(_universe.commander, _universe.crosshairX, _universe.crosshairY);

    const NearestSystem found = FindNearestSystem(_universe.commander.galaxySeeds, _universe.crosshairX, _universe.crosshairY,
                                                  _universe.commander.systemX, _universe.commander.systemY);
    _universe.selectedSeeds = found.seeds;
    _universe.crosshairX = found.x;
    _universe.crosshairY = found.y;

    CurrentSystemToCrosshairs(_universe.commander, _universe.crosshairX, _universe.crosshairY);

    // 6502: LDX #5 / likeTT112: LDA QQ15,X / STA QQ2,X / DEX / BPL likeTT112.
    _universe.current.seeds = _universe.selectedSeeds;

    /*
     * 6502: INX / STX EV.
     *
     * X is &FF here, because the loop above ended by decrementing past zero; the INX makes it zero.
     * So this is `EV = 0` written as an increment of whatever the last loop left, which is four
     * bytes cheaper than an LDA and is the only reason it reads the way it does.
     */
    _universe.explosions = 0;

    /*
     * 6502: LDA QQ3 / STA QQ28 / LDA QQ5 / STA tek / LDA QQ4 / STA gov.
     *
     * TT111 ends in a JMP to TT24, so QQ3 to QQ7 already describe the system it found -- these
     * three stores are caching them, not computing them, and the order (economy, TECH, government)
     * is not the order TT24 produced them in.
     */
    _universe.current.economy = found.data.economy;
    _universe.current.techLevel = found.data.techLevel;
    _universe.current.government = found.data.government;

    // 6502: and then BR1 runs off its end into BAY, which is the next routine in the binary.
    return EnterDockingBay(_universe.dockedFlag, _universe.view, _universe.status.hyperspaceCountdown, _hyperspaceHeld);
  }

  ForcedKey ResetAndStartGame(Universe& _universe, Ports& _ports, bool _hyperspaceHeld) noexcept
  {
    // 6502: TT170 -- LDX #&FF / TXS / JSR RESET, and RESET runs off its end into RES2, which is
    // why `ResetGame` ends with `ResetShipAndBubble` rather than this calling both.
    ResetGame(_universe, _ports, _universe.dockedFlag);

    // 6502: the fall-through into DEATH2 -- LDX #&FF / TXS / JSR RES2, a SECOND time.
    ResetShipAndBubble(_universe, _ports);

    // 6502: and then into BR1.
    return StartGame(_universe, _ports, _hyperspaceHeld);
  }

} // namespace Elite
