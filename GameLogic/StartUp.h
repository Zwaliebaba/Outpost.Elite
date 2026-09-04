#pragma once

#include "DockedKeys.h"
#include "SaveGame.h"
#include "Universe.h"

#include <cstdint>
#include <span>

namespace Elite
{

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
   * 6502: ping -- LDX #1 / pl1: LDA QQ0,X / STA QQ9,X / DEX / BPL pl1.
   *
   * The crosshairs to where the ship is, both coordinates, counting DOWN -- so the loop moves the y
   * first. It reads the COMMANDER, because QQ0 and QQ1 are two of its bytes.
   */
  void CrosshairsToCurrentSystem(const CommanderBlock& _commander, std::uint8_t& _crosshairX, std::uint8_t& _crosshairY) noexcept;

  /*
   * 6502: jmp -- the other direction, and it is what makes a hyperspace jump arrive.
   *
   * Two separate loads rather than a loop, which is why `hy5`'s RTS sits under it and three
   * routines return through that. And it writes into the commander: arriving somewhere is a change
   * to the saved game, not to a variable beside it.
   */
  void CurrentSystemToCrosshairs(CommanderBlock& _commander, std::uint8_t _crosshairX, std::uint8_t _crosshairY) noexcept;

  /*
   * What the start sequence reaches for outside GameLogic.
   *
   * Every one of these is either the flight model's or the machine's, and all of them are phase 3's
   * or the executable's. They are separate methods rather than one "start" because the ORDER is the
   * thing being ported, and an interface that bundled them would have nothing left to compare.
   */
  class StartUpEffects
  {
  public:
    virtual ~StartUpEffects() = default;

    /*
     * 6502: RESET, which falls into RES2 -- the whole universe and then the ship.
     *
     * RESET zeroes the ship slots, clears the roll and pitch, sets QQ12 to zero and clears the
     * fuel-scoop damage, and then runs off its end into RES2. So a caller of RESET gets both, and
     * that is not visible from the call site.
     */
    virtual void ResetUniverse() = 0;

    /// 6502: RES2 on its own -- the ship, the line heap, the dashboard, the missile lock and the
    /// stardust. DEATH2 enters here, and so does TT170 a second time (see ResetAndStartGame).
    virtual void ResetShip() = 0;

    /*
     * 6502: ZEKTRAN -- zero the key logger and `thiskey`.
     *
     * Sixty-five bytes of KEYLOOK, one per key the game watches. It is keyboard state and belongs
     * with the key map in the executable. The routine ends in TWO consecutive RTS instructions,
     * the second of which nothing can reach.
     */
    virtual void ClearKeyLogger() = 0;

    /// 6502: startat -- begin the title theme on the SID.
    virtual void StartTheme() = 0;

    /// 6502: stopat -- stop it, and silence all three voices.
    virtual void StopTheme() = 0;

    /// 6502: msblob -- the dashboard's missile indicators, green up to NOMSL and black above it.
    virtual void ResetMissileIndicators() = 0;

    /// 6502: LAUN -- the space station's docking tunnel, drawn as a sequence of expanding circles.
    /// Arriving reaches it; `DOENTRY` is its only caller here.
    virtual void ShowDockingTunnel() = 0;

    /*
     * 6502: DELAY -- wait for _frames VERTICAL SYNCS.
     *
     * Declared here as well as on `LineEntryEffects`, deliberately, and for the reason that one
     * says: two independent statements of what a routine needs rather than one interface
     * pretending to be shared. The executable satisfies both with one object.
     */
    virtual void WaitFrames(std::uint8_t _frames) = 0;

    /*
     * 6502: TITLE -- a rotating ship, a token under it, and a wait for a key.
     *
     * Returns the key that ended it, which BR1 compares against "Y". The ship rotates through the
     * flight model's own projection (`LL9`), so this waits on phase 3b and is a seam rather than a
     * screen; `_distance` is how far away it settles once it has finished moving towards the
     * viewer, and it is 210 for the Cobra and 48 for the Adder.
     */
    [[nodiscard]] virtual std::uint8_t ShowTitleScreen(std::uint8_t _token, std::uint8_t _shipType, std::uint8_t _distance) = 0;
  };

  /// 6502: the two title screens BR1 shows, which differ in every argument.
  inline constexpr std::uint8_t TITLE_LOAD_TOKEN = 6;  ///< "LOAD NEW COMMANDER (Y/N)?"
  inline constexpr std::uint8_t TITLE_START_TOKEN = 7; ///< "PRESS FIRE OR SPACE, COMMANDER."
  inline constexpr std::uint8_t SHIP_COBRA_MK3 = 11;   ///< 6502: CYL
  inline constexpr std::uint8_t SHIP_ADDER = 20;       ///< 6502: ADA
  inline constexpr std::uint8_t TITLE_COBRA_DISTANCE = 210;
  inline constexpr std::uint8_t TITLE_ADDER_DISTANCE = 48;

  /// 6502: YINT -- the internal key number for "Y", which is the only answer BR1 acts on.
  inline constexpr std::uint8_t KEY_YES_INTERNAL = 0x27;

  /// 6502: LDA #3 / JSR DOXC -- where the title screen's prompt starts.
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
   * `LDA QQ12 / BEQ P%+5 / JMP MLOOP / JMP TT100`, and the branch is easy to read backwards: `BEQ`
   * skips the three bytes of `JMP MLOOP`, so it is a ZERO QQ12 that reaches TT100. Docked goes to
   * MLOOP, which is the loop's second half; in space goes to TT100, which is all of it.
   */
  [[nodiscard]] ForcedKey ForceKey(std::uint8_t _key, std::uint8_t _dockedFlag, std::uint8_t _view, std::uint8_t _countdown,
                                   bool _hyperspaceHeld) noexcept;

  /*
   * Everything the start sequence works on.
   *
   * One struct for the reason `TradeScreen` and `SaveScreen` are structs: the alternative is a
   * function with a dozen arguments, written twice. `save` is here because BR1 offers the disk
   * menu, which is the one place the title screen reaches all the way into slice 2d.
   */
  struct GameStart
  {
    StartUpEffects& effects;
    SaveScreen& save;
    TextState& text;

    CommanderBlock& commander;                          ///< 6502: TP, through NAME
    std::span<std::uint8_t, COMMANDER_NAME_SIZE> name;  ///< 6502: NAME
    std::span<std::uint8_t, COMMANDER_FILE_SIZE> image; ///< 6502: NA%
    std::span<std::uint8_t> buffer;                     ///< 6502: INWK+5, the line editor's
    bool& useDisk;                                      ///< 6502: DISK

    CurrentSystem& current;
    SystemSeeds& selected;        ///< 6502: QQ15
    std::uint8_t& crosshairX;     ///< 6502: QQ9
    std::uint8_t& crosshairY;     ///< 6502: QQ10
    std::uint8_t& explosionCount; ///< 6502: EV

    /*
     * What the fall-through into BAY needs, which is the dispatch's state rather than the start
     * sequence's. None of it can change the answer for the key BAY forces -- "8" is settled in
     * TT102's first block, above every test of a view or a counter -- but the port passes what the
     * original would have had rather than assuming that stays true.
     */
    std::uint8_t& dockedFlag;    ///< 6502: QQ12
    std::uint8_t view = 0;       ///< 6502: QQ11
    std::uint8_t countdown = 0;  ///< 6502: QQ22+1
    bool hyperspaceHeld = false; ///< 6502: KLO+HINT
  };

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
  [[nodiscard]] ForcedKey StartGame(GameStart& _game) noexcept;

  /*
   * 6502: TT170, which falls through DEATH2 into BR1 -- the cold start.
   *
   * The reset runs TWICE and neither call is written down as such. TT170's `JSR RESET` gets RES2 as
   * well, because RESET has no RTS and runs off its end into it; the routine then falls into DEATH2,
   * whose own `JSR RES2` runs it a second time. Reproduced rather than collapsed: RES2 is not
   * idempotent in the original (it toggles the energy bomb off, and stops the bulletin board), so
   * calling it once would be a different game.
   *
   * `LDX #&FF / TXS` -- twice, once here and once in DEATH2 -- resets the 6502 stack pointer, which
   * is how the original discards whatever frames the death or the start left behind. There is no
   * port equivalent and none is needed: the port's callers return normally.
   */
  [[nodiscard]] ForcedKey ResetAndStartGame(GameStart& _game) noexcept;

  /*
   * 6502: BAY -- go to the docking bay.
   *
   * Four instructions: set QQ12 to &FF, and force key "8". So "arriving at the station" is, to the
   * game, indistinguishable from the player pressing the status key while docked -- and the docked
   * flag is set to &FF rather than to 1, which is what makes `TT102`'s `BIT QQ12 / BPL` work.
   */
  [[nodiscard]] ForcedKey EnterDockingBay(std::uint8_t& _dockedFlag, std::uint8_t _view, std::uint8_t _countdown,
                                          bool _hyperspaceHeld) noexcept;

} // namespace Elite
