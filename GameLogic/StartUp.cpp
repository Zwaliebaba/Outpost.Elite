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

  void CrosshairsToCurrentSystem(Universe& _universe) noexcept
  {
    // 6502: ping -- and QQ0 is TP+1, so this reads the commander block itself.
    _universe.crosshairX = _universe.commander.systemX;
    _universe.crosshairY = _universe.commander.systemY;
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

    // 6502: TT102 -- a real call, so the dispatch happens before the loop is chosen.
    result.outcome = ActionForKey(_key, _dockedFlag, _view, _countdown, _hyperspaceHeld);

    // 6502: FRCE -- a zero `QQ12` branches PAST the first of two jumps, so zero is the flight loop
    // and non-zero the docked one.
    result.loop = (_dockedFlag != 0u) ? MainLoop::Docked : MainLoop::InSpace;
    return result;
  }

  ForcedKey EnterDockingBay(Universe& _universe, std::uint8_t _view, std::uint8_t _countdown, bool _hyperspaceHeld) noexcept
  {
    // 6502: &FF rather than 1, which is what TT102's sign test needs.
    _universe.dockedFlag = 0xFF;

    // 6502: the status key, pressed by the game on the player's behalf.
    return ForceKey(KEY_STATUS, _universe.dockedFlag, _view, _countdown, _hyperspaceHeld);
  }

  ForcedKey StartGame(Universe& _universe, Ports& _ports, bool _hyperspaceHeld) noexcept
  {
    // 6502: ZEKTRAN -- the key logger, before anything can be typed at it.
    _universe.keys.fill(0u);

    // 6502: the prompt's column.
    _universe.text.column = TITLE_PROMPT_COLUMN;

    // 6502: JSR startat.
    StartTheme(_universe.music, _universe.memoryMap, _ports.sid);

    // 6502: TITLE with a Cobra Mk III, a long way off.
    const std::uint8_t answer = ShowTitleShip(_universe, _ports, TITLE_LOAD_TOKEN, ShipType::CobraMk3, TITLE_COBRA_DISTANCE);

    /*
     * 6502: QU5 -- the answer compared against "Y".
     *
     * ONLY "Y" opens the menu. Every other key -- including "N", including a joystick fire -- falls
     * straight through to the shared tail, so there is no way to answer this question wrongly.
     */
    if (answer == KEY_YES_INTERNAL)
    {
      // 6502: the music stopped, the default commander loaded, the menu, and the music again.
      StopMusic(_universe.music, _universe.sound, _universe.memoryMap, _ports.sid);

      // 6502: DFAULT -- so the menu has a commander to print a name for.
      (void)LoadCommander(_universe.commanderFile, _universe.commander, _universe.commanderName);

      (void)DiskAccessMenu(_universe, _ports);

      /*
       * 6502: JSR startat -- and the theme restarts from the beginning.
       *
       * BR1 ignores SVE's carry, which every other caller reads. TT102 uses it to decide between
       * restarting the game and returning to the bay; here both answers lead to the same next
       * instruction, because `QU5`'s DFAULT below installs whatever the menu left in the image.
       */
      StartTheme(_universe.music, _universe.memoryMap, _ports.sid);
    }

    /*
     * 6502: QU5 -- `DFAULT`, and this label is reached from three places: the "not Y" branch above,
     * the fall-through from the disk menu, and TT102's jump to it when a load succeeded.
     */
    (void)LoadCommander(_universe.commanderFile, _universe.commander, _universe.commanderName);

    // 6502: JSR msblob.
    ResetMissileIndicators(_universe.canvas, _universe.commander.missiles, &_universe.picture);

    // 6502: TITLE with an Adder, close up. Its key is discarded.
    (void)ShowTitleShip(_universe, _ports, TITLE_START_TOKEN, ShipType::Adder, TITLE_ADDER_DISTANCE);

    // 6502: JSR stopat -- the only stop both paths reach.
    StopMusic(_universe.music, _universe.sound, _universe.memoryMap, _ports.sid);

    /*
     * 6502: ping, TT111 and jmp -- and this SNAPS the commander's position.
     *
     * The crosshairs go to where the commander says it is, the search finds the nearest system that
     * the generator actually produces and writes its coordinates back over the crosshairs, and then
     * they are copied into the commander. A file whose coordinates fall between two systems starts
     * the game at whichever one was nearest.
     */
    CrosshairsToCurrentSystem(_universe);

    const NearestSystem found = FindNearestSystem(_universe.commander.galaxySeeds, _universe.crosshairX, _universe.crosshairY,
                                                  _universe.commander.systemX, _universe.commander.systemY);
    _universe.selectedSeeds = found.seeds;
    _universe.crosshairX = found.x;
    _universe.crosshairY = found.y;

    CurrentSystemToCrosshairs(_universe.commander, _universe.crosshairX, _universe.crosshairY);

    // 6502: likeTT112 -- the six seed bytes copied down, counting X to zero.
    _universe.current.seeds = _universe.selectedSeeds;

    /*
     * 6502: X STEPPED UP AND STORED into `EV`.
     *
     * X is &FF here, because the loop above ended by decrementing past zero; the INX makes it zero.
     * So this is `EV = 0` written as an increment of whatever the last loop left, which is four
     * bytes cheaper than an LDA and is the only reason it reads the way it does.
     */
    _universe.explosions = 0;

    /*
     * 6502: the economy, tech level and government cached out of the generator's own bytes.
     *
     * TT111 ends in a JMP to TT24, so QQ3 to QQ7 already describe the system it found -- these
     * three stores are caching them, not computing them, and the order (economy, TECH, government)
     * is not the order TT24 produced them in.
     */
    _universe.current.economy = found.data.economy;
    _universe.current.techLevel = found.data.techLevel;
    _universe.current.government = found.data.government;

    // 6502: and then BR1 runs off its end into BAY, which is the next routine in the binary.
    return EnterDockingBay(_universe, _universe.view, _universe.status.hyperspaceCountdown, _hyperspaceHeld);
  }

  ForcedKey ResetAndStartGame(Universe& _universe, Ports& _ports, bool _hyperspaceHeld) noexcept
  {
    // 6502: TT170 -- the stack reset and `RESET`, and `RESET` runs off its end into `RES2`, which
    // is why `ResetGame` ends with `ResetShipAndBubble` rather than this calling both.
    ResetGame(_universe, _ports);

    // 6502: the fall-through into DEATH2 -- the stack reset and `RES2`, a SECOND time.
    ResetShipAndBubble(_universe, _ports);

    // 6502: and then into BR1.
    return StartGame(_universe, _ports, _hyperspaceHeld);
  }

} // namespace Elite
