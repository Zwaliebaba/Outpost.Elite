#pragma once

#include "ExtendedTokens.h"
#include "FlightLoop.h"
#include "StartUp.h"

#include <cstdint>

namespace Elite
{

  /*
   * The missions (slice 4d).
   *
   * 6502: BRIEF, BRIEF2, BRIEF3, BRP, BRIS, DEBRIEF, DEBRIEF2 and TBRIEF, plus the four control
   * codes their tokens contain -- PAUSE, PAS1, PAUSE2 and MT9 -- and MT27 and MT28, which print a
   * name that depends on which galaxy the player is in.
   *
   * WHY A BRIEFING IS NOT A SCREEN. Elite has no mission-briefing screen: a briefing is an extended
   * TOKEN, printed by the same `DETOK` that prints a system description, and what makes it a
   * briefing rather than a paragraph is that four of the control codes inside it stop and wait.
   * Token 10 contains `{22}` twice, which spins the Constrictor and waits for a key; token 223
   * contains `{24}`, which waits without a ship; every one of them opens with `{25}`, which is
   * "INCOMING MESSAGE" and a two-second pause. So the routines below are the ones the TEXT calls,
   * and the missions are eleven instructions of state around them.
   */

  /// 6502: conhieght -- "the size of the gap left for the rotating Constrictor", and it is the
  /// ship's y coordinate rather than a gap: `PAS1` stores it straight into `INWK+3`.
  inline constexpr std::uint8_t BRIEFING_SHIP_HEIGHT = 80;

  /*
   * 6502: LDA #2 / STA INWK+7 -- and the upstream comment says "Set z_hi = 1", which this build
   * does not do.
   *
   * The Master and Apple versions load 1; the C64 loads 2, so the briefing ship sits at
   * (z_hi z_lo) = 512 rather than 256 -- twice as far away, and half the size. The comment was
   * written for the other machine and travelled. `tools/c64_source.py` is what stops a port
   * inheriting it (AGENTS §6), and the byte is asserted against the assembled image.
   */
  inline constexpr std::uint8_t BRIEFING_SHIP_DISTANCE = 2;

  /// 6502: LDA #216 / JSR DETOK -- "{clear screen}{tab 6}{move to row 10}{all caps}INCOMING
  /// MESSAGE", which is the token every briefing opens with.
  inline constexpr std::uint8_t INCOMING_MESSAGE_TOKEN = 216;

  /// 6502: LDY #100 / JMP DELAY -- a hundred vertical syncs, so two seconds on PAL and 1.67 on
  /// NTSC (§6.17).
  inline constexpr std::uint8_t INCOMING_MESSAGE_FRAMES = 100;

  /// 6502: LDA #1 / JSR DOXC / JMP TT66 -- MT9's column and its view, which are the same byte:
  /// `STA` does not touch A, so `TT66` is entered with the 1 that `DOXC` was given.
  inline constexpr std::uint8_t MT9_COLUMN_AND_VIEW = 1;

  /// 6502: MT23's `LDA #10` and MT29's `LDA #6` -- the row each moves to, and the ONLY thing they
  /// do to the cursor. `DOYC` is `STA YC / RTS`; neither touches the column.
  inline constexpr std::uint8_t MT23_ROW = 10;
  inline constexpr std::uint8_t MT29_ROW = 6;

  /*
   * 6502: MT27's `LDA #217` and MT28's `LDA #220`, both `CLC / ADC GCNT`.
   *
   * MT27 is the mission captain's name and MT28 the planet the Constrictor was last seen at, and
   * both are one token per galaxy. MT27 skips MT28's load with `BNE P%+4`, so the two are one
   * routine with two entry points and one addition.
   */
  inline constexpr std::uint8_t MISSION_CAPTAIN_TOKEN = 217;
  inline constexpr std::uint8_t MISSION_PLANET_TOKEN = 220;

  /*
   * Everything the mission text reaches that `FlightLoop` does not already carry.
   *
   * A struct for the same reason `TitleScreen` is one: the alternative is a five-argument function
   * repeated four times. `PAUSE` needs the flight model (it draws a ship through `LL9` and turns it
   * through `MVEIT`), the keyboard, and the screen -- which is `TITLE`'s list minus the joystick
   * question and the docked flag.
   */
  struct MissionScreen
  {
    FlightLoop& loop;
    StartUpEffects& effects;      ///< 6502: RDKEY, through `ScanTitleKeys`, and `DELAY`
    ExtendedTokenPrinter& tokens; ///< 6502: DETOK
    KeyLogger& keys;              ///< 6502: KLO -- what the scan fills in

    /*
     * 6502: INF -- which slot holds the ship being shown, as a number rather than a pointer.
     *
     * `LL9` part 1 writes two bytes of the ship's block directly rather than waiting for `INWK` to
     * be copied back, so it needs the block as well as the workspace. In the original that is `INF`
     * and it is left pointing at whatever `NWSHP` created; here the slot travels in the struct,
     * because `PAUSE` runs INSIDE the token that `BRIEF` is printing and has to find the ship that
     * `BRIEF` made several hundred instructions earlier.
     */
    std::uint8_t shipSlot = 0;
  };

  /*
   * 6502: PAS1 -- put the briefing ship back where it belongs, draw it, turn it, read the keyboard.
   *
   * IT RESETS THE POSITION EVERY FRAME, which is what makes the ship turn on the spot rather than
   * drift: `MVEIT` moves it and this puts x, z and y straight back before the next draw. The same
   * trick `TITLE` uses, spelled differently -- there the three stores are after `MVEIT` and here
   * they are before `LL9`, which is the same loop entered at a different point.
   *
   * Returns what `RDKEY` returns. `PAUSE`'s two loops branch on `thiskey` alone, so the carry is
   * carried for completeness rather than because this caller reads it.
   */
  [[nodiscard]] TitleKey ShowBriefingShip(MissionScreen& _mission) noexcept;

  /*
   * 6502: PAUSE, control code 22 -- spin the ship until a key is pressed, then put it away.
   *
   * TWO LOOPS AND THE FIRST ONE IS THE INTERESTING ONE. `JSR PAS1 / BNE PAUSE` runs while a key is
   * being HELD, so the routine first waits for the player to let go of whatever dismissed the
   * previous page, and only then waits for the next press. Without it one keystroke would dismiss
   * every remaining page of a briefing in a single frame.
   *
   * IT FALLS INTO MT23, and the port splits that the way the printer already splits codes 23 and
   * 29: the cursor move is here and the two case flags are the printer's. A control-code handler
   * that forgot the fall-through would print the rest of the briefing in the wrong case.
   */
  void PauseForKey(MissionScreen& _mission) noexcept;

  /*
   * 6502: PAUSE2, control code 24 -- the same wait with no ship in it.
   *
   * `JSR RDKEY / BNE PAUSE2 / JSR RDKEY / BEQ PAUSE2` and the second branch goes back to the FIRST
   * scan, not to the second -- so a key that arrives is checked once more for release before the
   * routine will look for it again. The label after the `RTS` is `newyearseve`, which is the only
   * clue in the source about when it was written.
   */
  void WaitForKeyPress(MissionScreen& _mission) noexcept;

  /*
   * 6502: BRIS, control code 25 -- "INCOMING MESSAGE" and two seconds of nothing.
   *
   * `LDA #216 / JSR DETOK / LDY #100 / JMP DELAY`. Token 216 clears the screen itself, so this is
   * the whole of the transition into a briefing.
   */
  void ShowIncomingMessage(MissionScreen& _mission) noexcept;

  /*
   * 6502: MT27 and MT28 -- one routine, two entry points, one addition.
   *
   * `_base` is 217 for the captain and 220 for the planet, and `GCNT` is added to it, so each is
   * eight consecutive tokens and the two ranges OVERLAP: galaxy 3's captain and galaxy 0's planet
   * are the same token. That is the table's business rather than a defect, and it is worth knowing
   * before reading the token dump and concluding something is wrong.
   */
  void PrintMissionToken(ExtendedTokenPrinter& _tokens, std::uint8_t _base, std::uint8_t _galaxy) noexcept;

  /*
   * 6502: the entries of `JMTB` that a mission briefing reaches -- 8, 9, 22, 23, 24, 25, 27, 28
   * and 29.
   *
   * WHY THE DISPATCH IS HERE AND NOT IN THE EXECUTABLE. It was in the executable, because every one
   * of these codes needed something the executable had and `GameLogic` did not: a canvas to clear,
   * a keyboard to wait on, a galaxy number. All three arrived -- `TT66` in slice 3d, `RDKEY`'s seam
   * in 3b, the commander block in 2d -- and what was left in `Outpost/Shell.cpp` was nine cases of
   * arithmetic that no test could reach, two of which turned out to be wrong: code 9 was not moving
   * the cursor and codes 23 and 29 were moving it when the game does not.
   *
   * The shell still owns code 21, which is `CLYNS` and belongs to the docked screens, and the codes
   * nothing yet answers. It forwards the rest here.
   */
  class MissionCodes final : public ControlCodes
  {
  public:
    MissionCodes(MissionScreen& _mission, TextState& _text, const std::uint8_t& _galaxy) noexcept
      : m_mission(_mission),
        m_text(_text),
        m_galaxy(_galaxy)
    {
    }

    /// Runs `_code` and says whether it was one of this object's. A code it does not know is left
    /// entirely alone, so the caller's own switch can have it.
    [[nodiscard]] bool RunMissionCode(std::uint8_t _code) noexcept;

    /// `ControlCodes`, for a caller that has nothing else to add. Ignores what it does not know.
    void Run(std::uint8_t _code) override
    {
      static_cast<void>(RunMissionCode(_code));
    }

  private:
    MissionScreen& m_mission;
    TextState& m_text;
    const std::uint8_t& m_galaxy;
  };

  /// 6502: MT8 -- LDA #6 / JSR DOXC, and the `DTW2` store beside it is the printer's.
  inline constexpr std::uint8_t MT8_COLUMN = 6;

  // ---- the missions themselves (slice 4d-c) ---------------------------------------------------

  /*
   * 6502: TP -- the mission state, and mission 1's two bits mean FOUR things (slice 2e).
   *
   * `%00` is not started, `%01` is in progress, `%11` is "in progress AND complete" -- the state
   * `MissionOnDocking` reads as "you have only just finished it" -- and `%10` is finished and paid.
   * So the pair is a small state machine, and `BRIEF` and `DEBRIEF` are the two transitions that
   * are not a docking test.
   */
  inline constexpr std::uint8_t MISSION_1_STARTED = 0x01; ///< 6502: LSR TP / SEC / ROL TP
  inline constexpr std::uint8_t MISSION_2_STARTED = 0x04; ///< 6502: ORA #%00000100
  inline constexpr std::uint8_t MISSION_2_PLANS = 0x0A;   ///< 6502: ORA #%00001010, after AND #%11110000
  inline constexpr std::uint8_t MISSION_2_KEEP = 0xF0;    ///< 6502: AND #%11110000 -- `BRIEF3` clears the low nibble
  inline constexpr std::uint8_t MISSION_TRUMBLES = 0x10;  ///< 6502: ORA #%00010000

  /// 6502: LDA #64 / STA MCNT -- how many frames the Constrictor turns for before it starts to
  /// move away, and the counter goes on counting DOWN through the second loop without being read.
  inline constexpr std::uint8_t BRIEFING_SPIN_FRAMES = 64;

  /// 6502: LDX #%01111111 -- the roll and pitch counters at maximum with the damping bit clear, so
  /// the ship turns and keeps turning. Written INSIDE the loop, once per frame.
  inline constexpr std::uint8_t BRIEFING_SPIN = 0x7F;

  /// 6502: LDA #1 / JSR DOXC / STA INWK+7 -- one load again, and this one is a column and a
  /// DISTANCE. `TT66` two instructions later gets the same 1 as its view.
  inline constexpr std::uint8_t BRIEFING_START_DISTANCE = 1;

  /// 6502: the tokens each mission prints. 10 and 15 are the Constrictor's briefing and its thank
  /// you, 11, 222 and 223 are the Thargoid plans' three, and 199 offers the Trumble.
  inline constexpr std::uint8_t MISSION_1_BRIEFING = 10;
  inline constexpr std::uint8_t MISSION_1_DEBRIEFING = 15;
  inline constexpr std::uint8_t MISSION_2_CONTACT = 11;
  inline constexpr std::uint8_t MISSION_2_BRIEFING = 222;
  inline constexpr std::uint8_t MISSION_2_DEBRIEFING = 223;
  inline constexpr std::uint8_t TRUMBLE_OFFER = 199;

  /// 6502: LDX #LO(50000) / LDY #HI(50000) -- 5,000 credits, in the tenths the cash is held in.
  /// The same number is the reward for mission 1 and the price of a Trumble.
  inline constexpr std::uint16_t MISSION_REWARD = 50000;

  /// 6502: LDA #2 / STA ENGY -- the navy's energy unit, which recharges at three units a pass
  /// where the shop's does two. Two is the byte and three is what the flight loop makes of it.
  inline constexpr std::uint8_t NAVY_ENERGY_UNIT = 2;

  /*
   * What a mission needs beyond the text: the commander it changes, and the docking bay it ends at.
   *
   * `BRP` is `JSR DETOK` then `JMP BAY`, and every mission but `TBRIEF`'s refusal is a tail call
   * into it -- so all seven return the same thing, and it is the forced key `BAY` presses.
   */
  struct MissionBay
  {
    CommanderBlock& commander; ///< 6502: TP, CASH, ENGY, TALLY and TRIBBLE
    std::uint8_t& dockedFlag;  ///< 6502: QQ12, which `BAY` sets to &FF
    std::uint8_t view;         ///< 6502: QQ11, for the forced key's dispatch
    std::uint8_t countdown;    ///< 6502: QQ22+1 -- an in-flight hyperspace countdown, which is zero here
    bool hyperspaceHeld = false;
  };

  /*
   * 6502: BRP -- print an extended token and go to the docking bay.
   *
   * `JSR DETOK` then `.BAYSTEP JMP BAY`, and `BAYSTEP` is the entry `TBRIEF` uses when the player
   * turns the Trumble down: it skips the token and goes straight to the bay.
   */
  [[nodiscard]] ForcedKey PrintAndEnterBay(MissionScreen& _mission, MissionBay& _bay, std::uint8_t _token) noexcept;

  /*
   * 6502: BRIEF -- start mission 1, and show the Constrictor turning while it says so.
   *
   * THE SHIP IS BUILT INTO THE BRIEFING SCREEN'S OWN BUBBLE. `ZINF`, `NWSHP` and then a `TT66`
   * that clears the screen the ship was just added to -- so the Constrictor occupies a slot in the
   * docked game's ship list for as long as the briefing runs, which is why `RES2` is what tidies
   * up after it rather than anything here.
   *
   * TWO LOOPS AND ONE COUNTER. The first turns the ship on the spot for sixty-four frames; the
   * second moves it away and up until `z_lo` passes 255, and it goes on decrementing `MCNT` all
   * the way without ever reading it. So the counter is live in the first loop and dead in the
   * second, and a port that stopped decrementing it there would be caught by `MCNT` alone.
   *
   * THE SECOND LOOP INCREMENTS `z_lo` TWICE PER FRAME and tests after each, so the ship recedes at
   * two units a frame and the exit can be taken on either half. `LSR INWK` halves the x coordinate
   * every frame at the same time, which is what walks it back to the middle of the screen.
   *
   * Returns the token `BR2` leaves in the accumulator, which is 10 -- so this is `BRIEF` up to but
   * not including its tail call, and `BriefMission1` below is the whole of it. Split there because
   * that is where the original splits: `BR2` ends `LDA #10 / BNE BRPS`, and everything after the
   * branch is `BRP`'s and is shared with four other missions.
   */
  [[nodiscard]] std::uint8_t RunConstrictorBriefing(MissionScreen& _mission, MissionBay& _bay) noexcept;

  /// 6502: BRIEF, whole -- the briefing and then `BRP`, which prints token 10 and goes to the bay.
  [[nodiscard]] ForcedKey BriefMission1(MissionScreen& _mission, MissionBay& _bay) noexcept;

  /// 6502: BRIEF2 -- set bit 2 of `TP` and print token 11, the message that sends the player to
  /// Ceerdi. Falls into `BRP`.
  [[nodiscard]] ForcedKey BriefMission2(MissionScreen& _mission, MissionBay& _bay) noexcept;

  /*
   * 6502: BRIEF3 -- collect the plans at Ceerdi.
   *
   * `AND #%11110000 / ORA #%00001010` -- and the `AND` is the interesting half: it clears mission
   * 1's two bits as well as setting mission 2's, so picking up the plans is also what forgets that
   * the Constrictor ever happened. Bit 3 is "the plans are aboard".
   */
  [[nodiscard]] ForcedKey CollectPlans(MissionScreen& _mission, MissionBay& _bay) noexcept;

  /*
   * 6502: DEBRIEF -- finish mission 1 and pay for it.
   *
   * `LSR TP / ASL TP` clears bit 0 and nothing else, which leaves bit 1 standing: the pair goes
   * from `%11` to `%10`, which is the state `MissionOnDocking` reads as "finished and paid" and
   * will not offer again. The commented-out `INC TALLY+1` beside it is in the original source and
   * is not ported, because it does not run.
   */
  [[nodiscard]] ForcedKey DebriefMission1(MissionScreen& _mission, MissionBay& _bay) noexcept;

  /*
   * 6502: DEBRIEF2 -- deliver the plans at Birera.
   *
   * Three rewards in five instructions: bit 2 of `TP` set so both 2 and 3 are up, `ENGY` set to 2
   * -- the navy's energy unit, which recharges faster than the one the shop sells -- and 256 kill
   * points, added to the HIGH byte of the tally so the low byte is untouched.
   */
  [[nodiscard]] ForcedKey DebriefMission2(MissionScreen& _mission, MissionBay& _bay) noexcept;

  /*
   * 6502: TBRIEF -- offer the Trumble, and take the money if it is accepted.
   *
   * BIT 4 IS SET BEFORE THE QUESTION IS ASKED, so declining still marks the mission as offered and
   * it is never offered again. Accepting costs 50,000 tenths and `INC TRIBBLE` -- the LOW byte, so
   * the player leaves with one Trumble and the breeding in `MLOOP` does the rest.
   *
   * `JSR LCASH` AND ITS ANSWER IS DISCARDED. `INC TRIBBLE` follows unconditionally, and `LCASH`
   * puts the money back when it cannot afford the spend -- so a commander who is short gets the
   * Trumble for nothing. `MissionOnDocking` mostly prevents that by refusing to offer below a cash
   * threshold, but that test reads ONE BYTE of a four-byte value (slice 2e), so the band recurs
   * every 6,553.6 credits and a poor player inside one is offered a free Trumble. Ported rather
   * than fixed, and recorded in ADR-001 §6.
   */
  [[nodiscard]] ForcedKey OfferTrumble(MissionScreen& _mission, MissionBay& _bay, KeySource& _keys) noexcept;

} // namespace Elite
