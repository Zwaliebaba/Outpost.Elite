#pragma once

#include "Controls.h"
#include "DockedKeys.h"
#include "SaveGame.h"
#include "Galaxy.h"

#include <cstdint>
#include <span>

namespace Elite
{

  struct Universe; // Universe.h -- forward, because Universe.h includes this one for `CurrentSystem`
  struct Ports;    // Ports.h, likewise

  /*
   * Starting a game, and going back to the docking bay (slice 2e).
   *
   * 6502: TT170, DEATH2, BR1, QU5, BAY and FRCE -- five labels and a fall-through chain, which
   * between them are every way the game ever begins. TT170 is the cold start; DEATH2 is what a
   * death lands on; BR1 is the title sequence; BAY is the way back to the pad after a save or a
   * launch that did not happen.
   *
   * Almost all of what these reach is phase 3's -- the rotating ship needs the flight model's
   * projection, RESET clears the ship workspace, and the theme is a SID -- so the port is the
   * SEQUENCE and the state, over a seam for each of those. That is not a thin thing to get right.
   * The order the seams are reached in carries three details a reading does not: the reset runs
   * TWICE, the music stops and starts around one branch and not the other, and the current system
   * is SNAPPED to the nearest generated one before the game begins.
   */

  /*
   * 6502: QQ2, QQ28, tek and gov -- what the game caches about the system it is AT.
   *
   * WHERE it is, though, is not here. QQ0 and QQ1 are TP+1 and TP+2: the ship's galactic
   * coordinates live INSIDE the commander block, which is why they survive a save and why a
   * hyperspace jump is a change to the commander rather than to anything alongside it. QQ2, QQ28,
   * `tek` and `gov` sit outside the block -- eighty-five bytes further on -- and are rebuilt from
   * those coordinates whenever the game starts or arrives, so they are here.
   *
   * Elite keeps two systems at all times and they are easy to conflate. QQ15 is the one the
   * crosshairs are on, which every chart screen changes. This is the one the ship is at. The
   * economy, tech level and government are CACHED rather than regenerated, because the market
   * screen reads them on every redraw and TT24 is not cheap.
   */
  struct CurrentSystem
  {
    SystemSeeds seeds;           ///< 6502: QQ2
    std::uint8_t economy = 0;    ///< 6502: QQ28
    std::uint8_t techLevel = 0;  ///< 6502: tek
    std::uint8_t government = 0; ///< 6502: gov
  };

  /*
   * 6502: ping -- put the crosshairs on the system the ship is at.
   *
   * The crosshairs to where the ship is, both coordinates, counting DOWN -- so the loop moves the y
   * first. It reads the COMMANDER, because QQ0 and QQ1 are two of its bytes.
   */
  void CrosshairsToCurrentSystem(Universe& _universe) noexcept;

  /*
   * 6502: jmp -- the other direction, and it is what makes a hyperspace jump arrive.
   *
   * Two separate loads rather than a loop, which is why `hy5`'s RTS sits under it and three
   * routines return through that. And it writes into the commander: arriving somewhere is a change
   * to the saved game, not to a variable beside it.
   */
  void CurrentSystemToCrosshairs(Commander& _commander, std::uint8_t _crosshairX, std::uint8_t _crosshairY) noexcept;

  /*
   * `StartUpEffects` WAS HERE AND IS NOT ANY MORE (M6-0-h-2).
   *
   * It was "what the start sequence reaches for outside GameLogic", and every one of its methods
   * turned out to be the library's: `ResetUniverse` and `ResetShip` (`RESET`, `RES2`) went in
   * M3-b-1e, `StartTheme` and `StopTheme` in M3-b-2b, `ShowDockingTunnel` when `LAUN` was ported
   * (§6.109), `ScanTitleKeys` in M3-b-3d -- 6502: RDKEY, whose answer `TitleKey` lives in
   * `Controls.h` beside `ScanKeyboard` -- and `WaitFrames` to `Presenter` in M3-b-3b,
   * `ClearKeyLogger` in M6-0-h-1 -- 6502: ZEKTRAN, which is `Universe::keys` zeroed by its
   * callers -- and `ShowTitleScreen` last: 6502: TITLE is `Elite::ShowTitleShip` (`Flight.h`),
   * and the executable had answered the seam by forwarding to it since §6.107. `BR1` calls it
   * directly rather than through anything, and a fixture that drives the start sequence runs the
   * title screen for real and ends it the way a player does -- with a key held.
   */

  /// 6502: the two title screens BR1 shows, which differ in every argument.
  inline constexpr std::uint8_t TITLE_LOAD_TOKEN = 6;  ///< "LOAD NEW COMMANDER (Y/N)?"
  inline constexpr std::uint8_t TITLE_START_TOKEN = 7; ///< "PRESS FIRE OR SPACE, COMMANDER."
  inline constexpr std::uint8_t TITLE_COBRA_DISTANCE = 210;
  inline constexpr std::uint8_t TITLE_ADDER_DISTANCE = 48;

  /// 6502: YINT -- the internal key number for "Y", which is the only answer BR1 acts on.
  inline constexpr std::uint8_t KEY_YES_INTERNAL = 0x27;

  /// 6502: the column DOXC is set to before the title screen's prompt is printed.
  inline constexpr std::uint8_t TITLE_PROMPT_COLUMN = 3;

  /// 6502: MLOOP and TT100 -- the two entries to the main game loop, which FRCE chooses between.
  enum class MainLoop
  {
    Docked,  ///< 6502: MLOOP -- reached when QQ12 is non-zero
    InSpace, ///< 6502: TT100 -- reached when it is zero
  };

  struct ForcedKey
  {
    KeyOutcome outcome; ///< what TT102 made of the key
    MainLoop loop = MainLoop::Docked;
  };

  /*
   * 6502: FRCE -- dispatch a key the game pressed for itself, then re-enter the main loop.
   *
   * The docked flag picks where to re-enter, and the branch is easy to read BACKWARDS: it fires
   * on QQ12 being ZERO and steps over the three bytes of the jump to `MLOOP`, so it is being IN
   * SPACE that reaches TT100. Docked goes to MLOOP, which is the loop's second half; in space
   * goes to TT100, which is all of it. The BBC form of this test branches the other way and its
   * commentary was carried over unchanged, so the obvious reading of the note beside it is the
   * wrong one.
   */
  [[nodiscard]] ForcedKey ForceKey(std::uint8_t _key, std::uint8_t _dockedFlag, std::uint8_t _view, std::uint8_t _countdown,
                                   bool _hyperspaceHeld) noexcept;

  // `GameStart` was fourteen references and four values, and it went with `SaveScreen` in M3-a-3:
  // it held one, because `BR1` offers the disk menu, and could not outlive it. Every byte of it is
  // `Universe`'s and the one seam is `Ports::start`. The last of the four values is the argument
  // below: what `KLO+HINT` held when the key was pressed, which the fall-through into `BAY` reads
  // and nothing in the universe carries.

  /*
   * 6502: BR1 -- the title sequence, and the start of a game.
   *
   * Three things in it are worth knowing before reading it.
   *
   * THE MUSIC BRACKETS ONE BRANCH AND NOT THE OTHER. `startat` runs before the first title screen;
   * answering "Y" stops it, runs the disk menu, and starts it again, so the theme plays through the
   * second title screen either way -- but a player who went into the menu hears it restart from the
   * beginning and one who did not hears it continue. `stopat` after the second screen is the only
   * one both paths reach.
   *
   * DFAULT RUNS TWICE ON THE "Y" PATH. Once before the menu, so the menu has a commander to show a
   * name for, and once at `QU5`, which is where the "N" path joins -- and which is also the label
   * `TT102` jumps to when the disk menu says a new commander was loaded. So the second call is not
   * redundant: it is the shared tail, reached from three places.
   *
   * AND THE CURRENT SYSTEM IS SNAPPED. `ping` puts the crosshairs on the commander's coordinates,
   * `TT111` finds the nearest GENERATED system to them and writes ITS coordinates back over the
   * crosshairs, and `jmp` copies those into the commander's. So a saved commander whose coordinates
   * fall between systems begins the game somewhere slightly different from where it was saved, and
   * QQ2 is then taken from the system that was found rather than from anything the file held.
   *
   * AND IT DOES NOT RETURN. BR1 runs off its end into BAY, so starting a game and arriving at the
   * docking bay are one instruction stream: the last thing the title sequence does is press "8" on
   * the player's behalf and enter the docked main loop. That is why this hands back a ForcedKey.
   */
  [[nodiscard]] ForcedKey StartGame(Universe& _universe, Ports& _ports, bool _hyperspaceHeld) noexcept;

  /*
   * 6502: TT170, which falls through DEATH2 into BR1 -- the cold start.
   *
   * The reset runs TWICE and neither call is written down as such. TT170's call to `RESET` gets
   * `RES2` as well, because `RESET` has no return of its own and runs off its end into it; the
   * routine then falls into DEATH2, whose own call runs `RES2` a second time. Reproduced rather
   * than collapsed: `RES2` is not idempotent in the original (it toggles the energy bomb off, and
   * stops the bulletin board), so calling it once would be a different game.
   *
   * The 6502 STACK POINTER is reset to the top -- twice, once here and once in DEATH2 -- which is
   * how the original discards whatever frames the death or the start left behind. There is no
   * port equivalent and none is needed: the port's callers return normally.
   */
  [[nodiscard]] ForcedKey ResetAndStartGame(Universe& _universe, Ports& _ports, bool _hyperspaceHeld) noexcept;

  /*
   * 6502: BAY -- go to the docking bay.
   *
   * Four instructions: set QQ12 to &FF, and force key "8". So "arriving at the station" is, to the
   * game, indistinguishable from the player pressing the status key while docked -- and the docked
   * flag is set to &FF rather than to 1, which is what lets `TT102` test it by its top bit alone.
   */
  [[nodiscard]] ForcedKey EnterDockingBay(Universe& _universe, std::uint8_t _view, std::uint8_t _countdown,
                                          bool _hyperspaceHeld) noexcept;

} // namespace Elite
