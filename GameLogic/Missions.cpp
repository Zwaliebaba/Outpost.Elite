#include "pch.h"

#include "Missions.h"

#include "EliteTypes.h"
#include "Market.h"
#include "PlanetDraw.h"
#include "SaveGame.h"
#include "ShipDraw.h"
#include "ShipMove.h"
#include "ViewChange.h"

namespace Elite
{

  namespace
  {
    /// 6502: JSR LL9 -- the briefing's ship, drawn from `INWK` with its block in `K%`.
    void DrawBriefingShip(MissionScreen& _mission) noexcept
    {
      FlightLoop& loop = _mission.loop;
      FlightScreen& screen = loop.screen;
      DrawShip(screen.canvas, screen.draw, screen.geometry, screen.math, loop.clip, loop.projection, screen.work,
               screen.bubble.blocks[_mission.shipSlot], loop.heap, screen.flight.blueprint, screen.flight.type, loop.drawing);
    }

    /*
     * 6502: JSR MVEIT -- and the answer is discarded, twice over.
     *
     * `MVEIT` only reaches `TACTICS` for a ship whose `INWK+32` has bit 7 set, and the Constrictor
     * `BRIEF` builds is made by `ZINF` and `NWSHP`, neither of which sets it. So the AI cannot run
     * here and there is nobody for it to kill -- the same argument `TITLE`'s ship rests on (§6.122).
     */
    void MoveBriefingShip(MissionScreen& _mission) noexcept
    {
      FlightLoop& loop = _mission.loop;
      FlightScreen& screen = loop.screen;
      static_cast<void>(
        MoveShip(screen.canvas, screen.draw, screen.work, screen.math, screen.flight, loop.tactics, screen.flight.blueprint, screen.view));
    }
  } // namespace

  TitleKey ShowBriefingShip(MissionScreen& _mission) noexcept
  {
    FlightLoop& loop = _mission.loop;
    FlightScreen& screen = loop.screen;

    /*
     * 6502: LDA #conhieght / STA INWK+3 / LDA #0 / STA INWK / STA INWK+6 / LDA #2 / STA INWK+7.
     *
     * Four stores that undo what `MVEIT` did to the position on the previous pass, which is what
     * makes the ship turn on the spot. Note the order: y first, then the two zeroes out of one
     * load, then the distance -- and `INWK+7` gets 2 on this build where the upstream comment
     * says 1 (`BRIEFING_SHIP_DISTANCE`).
     */
    screen.work[3] = BRIEFING_SHIP_HEIGHT;
    screen.work[0] = 0u;
    screen.work[6] = 0u;
    screen.work[7] = BRIEFING_SHIP_DISTANCE;

    // 6502: JSR LL9.
    DrawShip(screen.canvas, screen.draw, screen.geometry, screen.math, loop.clip, loop.projection, screen.work,
             screen.bubble.blocks[_mission.shipSlot], loop.heap, screen.flight.blueprint, screen.flight.type, loop.drawing);

    /*
     * 6502: JSR MVEIT.
     *
     * The answer is discarded for the same reason `TITLE`'s is: `MVEIT` only reaches `TACTICS` for
     * a ship whose `INWK+32` has bit 7 set, and the Constrictor `BRIEF` builds is made by `ZINF`
     * and `NWSHP` with no AI byte set, so there is nothing for the AI to do and nobody to do it to.
     */
    (void)MoveShip(screen.canvas, screen.draw, screen.work, screen.math, screen.flight, loop.tactics, screen.flight.blueprint, screen.view);

    // 6502: JMP RDKEY -- a tail call, so what `PAS1` returns is what `RDKEY` returns.
    return _mission.effects.ScanTitleKeys(_mission.keys);
  }

  void PauseForKey(MissionScreen& _mission) noexcept
  {
    FlightLoop& loop = _mission.loop;
    FlightScreen& screen = loop.screen;

    // 6502: .PAUSE JSR PAS1 / BNE PAUSE -- while a key is still HELD, which is the previous page's
    // keystroke not yet released.
    while (ShowBriefingShip(_mission).key != 0u)
    {
    }

    // 6502: .PAL1 JSR PAS1 / BEQ PAL1 -- and now wait for the next one.
    while (ShowBriefingShip(_mission).key == 0u)
    {
    }

    // 6502: LDA #0 / STA INWK+31 -- the ship is no longer drawn, so nothing will rub it out.
    screen.work[31] = 0u;

    // 6502: LDA #1 / JSR TT66 -- the space view again, cleared.
    SetUpScreen(screen, MT9_COLUMN_AND_VIEW);

    // 6502: JSR LL9 -- one more draw, onto the screen that was just cleared.
    DrawBriefingShip(_mission);

    /*
     * 6502: the fall-through into MT23 -- `LDA #10 / JSR DOYC`, and that is all of it that lands
     * here. `WHITETEXT` is a bare `RTS` on this build and `MT13`'s two stores are the printer's
     * state, so the control-code dispatch applies them after this returns, exactly as it does for
     * codes 23 and 29 themselves.
     */
    screen.text.row = MT23_ROW;
  }

  void WaitForKeyPress(MissionScreen& _mission) noexcept
  {
    for (;;)
    {
      // 6502: .PAUSE2 JSR RDKEY / BNE PAUSE2.
      if (_mission.effects.ScanTitleKeys(_mission.keys).key != 0u)
      {
        continue;
      }

      /*
       * 6502: JSR RDKEY / BEQ PAUSE2 -- and the branch goes back to the TOP, not to this scan.
       *
       * So a press that arrives here ends the routine, and a still-empty keyboard sends it back to
       * check for a release it has already had. Written as two scans in a loop rather than as a
       * do-while, because that is the shape: the first scan is reached again on every failure.
       */
      if (_mission.effects.ScanTitleKeys(_mission.keys).key != 0u)
      {
        return; // 6502: .newyearseve RTS
      }
    }
  }

  void ShowIncomingMessage(MissionScreen& _mission) noexcept
  {
    // 6502: LDA #216 / JSR DETOK -- and the token clears the screen itself.
    _mission.tokens.Print(INCOMING_MESSAGE_TOKEN);

    // 6502: LDY #100 / JMP DELAY.
    _mission.effects.WaitFrames(INCOMING_MESSAGE_FRAMES);
  }

  void PrintMissionToken(ExtendedTokenPrinter& _tokens, std::uint8_t _base, std::uint8_t _galaxy) noexcept
  {
    // 6502: CLC / ADC GCNT / BNE DETOK -- eight consecutive tokens, one per galaxy, and the `BNE`
    // is unconditional because neither base can sum to zero for a galaxy below eight.
    _tokens.Print(AddWithCarry(_base, _galaxy, false).value);
  }

  bool MissionCodes::RunMissionCode(std::uint8_t _code) noexcept
  {
    switch (_code)
    {
    case 8:
      // 6502: MT8 -- LDA #6 / JSR DOXC. The `DTW2` store is the printer's and is already done.
      m_text.column = MT8_COLUMN;
      return true;

    case 9:
      /*
       * 6502: MT9 -- LDA #1 / JSR DOXC / JMP TT66.
       *
       * ONE `LDA #1` DOES BOTH. `DOXC` is `STA XC / RTS` and `STA` does not touch the accumulator,
       * so the byte that became the column is still in A when `TT66` reads it as the view.
       */
      m_text.column = MT9_COLUMN_AND_VIEW;
      SetUpScreen(m_mission.loop.screen, MT9_COLUMN_AND_VIEW);
      return true;

    case 22:
      // 6502: PAUSE. Its fall-through into MT23 sets the row here and the case flags in the
      // printer, which is the same split codes 23 and 29 already have.
      PauseForKey(m_mission);
      return true;

    case 23:
    case 29:
      /*
       * 6502: MT23's `LDA #10` and MT29's `LDA #6`, both into `DOYC`.
       *
       * THE ROW AND NOTHING ELSE. `DOYC` is `STA YC / RTS`; neither entry point touches `XC`, so a
       * briefing that moves to row 10 keeps whatever column it was printing at. `WHITETEXT` is a
       * bare `RTS` on this build and `MT13`'s two stores are the printer's.
       */
      m_text.row = (_code == 23) ? MT23_ROW : MT29_ROW;
      return true;

    case 24:
      // 6502: PAUSE2 -- the same wait with no ship, and no fall-through after it.
      WaitForKeyPress(m_mission);
      return true;

    case 25:
      ShowIncomingMessage(m_mission);
      return true;

    case 27:
    case 28:
      // 6502: MT27 and MT28 -- the captain and the planet, one token per galaxy.
      PrintMissionToken(m_mission.tokens, (_code == 27) ? MISSION_CAPTAIN_TOKEN : MISSION_PLANET_TOKEN, m_galaxy);
      return true;

    default:
      return false;
    }
  }

  // ---- the missions themselves (slice 4d-c) ---------------------------------------------------

  ForcedKey PrintAndEnterBay(MissionScreen& _mission, MissionBay& _bay, std::uint8_t _token) noexcept
  {
    // 6502: JSR DETOK -- and `BAYSTEP`, the entry that skips it, is the caller passing no token.
    if (_token != 0u)
    {
      _mission.tokens.Print(_token);
    }

    // 6502: .BAYSTEP JMP BAY -- a tail call, so what a mission returns is what `BAY` returns.
    return EnterDockingBay(_bay.dockedFlag, _bay.view, _bay.countdown, _bay.hyperspaceHeld);
  }

  std::uint8_t RunConstrictorBriefing(MissionScreen& _mission, MissionBay& _bay) noexcept
  {
    FlightLoop& loop = _mission.loop;
    FlightScreen& screen = loop.screen;

    /*
     * 6502: LSR TP / SEC / ROL TP -- set bit 0, in three instructions and no `ORA`.
     *
     * The `LSR` shifts bit 0 out into the carry and a zero into bit 7; `SEC` then `ROL` shifts it
     * all back with a one going into bit 0. Every other bit ends where it started, so it is
     * `ORA #1` written for a machine whose author preferred shifts.
     */
    std::uint8_t& progress = _bay.commander.At(Field::MissionProgress);
    progress = static_cast<std::uint8_t>(progress | MISSION_1_STARTED);

    ShowIncomingMessage(_mission); // 6502: JSR BRIS

    ClearShipBlock(screen.work); // 6502: JSR ZINF

    // 6502: LDA #CON / STA TYPE / JSR NWSHP -- into the DOCKED game's bubble, which is why `RES2`
    // is what clears it up afterwards rather than anything here.
    screen.flight.type = SHIP_TYPE_CONSTRICTOR;
    const NewShip created = AddShip(screen.bubble, screen.work, SHIP_TYPE_CONSTRICTOR, screen.flight.blueprint);
    _mission.shipSlot = created.created ? created.slot : std::uint8_t{0};

    /*
     * 6502: LDA #1 / JSR DOXC / STA INWK+7 / JSR TT66.
     *
     * ONE LOAD AND THREE USES. `DOXC` is `STA XC / RTS` and leaves A alone, so the same 1 becomes
     * the text column, the ship's distance and `TT66`'s view -- which is the third time in this
     * file that a `JSR` between two stores is a way of not reloading the accumulator.
     */
    screen.text.column = BRIEFING_START_DISTANCE;
    screen.work[7] = BRIEFING_START_DISTANCE;
    SetUpScreen(screen, BRIEFING_START_DISTANCE);

    // 6502: LDA #64 / STA MCNT.
    screen.flight.mainLoopCounter = BRIEFING_SPIN_FRAMES;

    // 6502: .BRL1 -- sixty-four frames of the ship turning on the spot.
    do
    {
      // 6502: LDX #%01111111 / STX INWK+29 / STX INWK+30, INSIDE the loop: the counters are
      // rewritten every frame, so the damping `MVEIT` applies never gets a chance to take hold.
      screen.work[29] = BRIEFING_SPIN;
      screen.work[30] = BRIEFING_SPIN;

      DrawBriefingShip(_mission); // 6502: JSR LL9
      MoveBriefingShip(_mission); // 6502: JSR MVEIT

      screen.flight.mainLoopCounter = static_cast<std::uint8_t>(screen.flight.mainLoopCounter - 1u);
    } while (screen.flight.mainLoopCounter != 0u); // 6502: DEC MCNT / BNE BRL1

    /*
     * 6502: .BRL2 -- and the counter goes on counting down here without ever being read again.
     *
     * `DEC MCNT` is in this loop too and nothing branches on it, so `MCNT` is live in the first
     * loop and dead in the second. It is decremented anyway, because a port that stopped would
     * leave the byte the next screen inherits at the wrong value.
     */
    for (;;)
    {
      screen.work[0] = static_cast<std::uint8_t>(screen.work[0] >> 1); // 6502: LSR INWK

      /*
       * 6502: INC INWK+6 / BEQ BR2 / INC INWK+6 / BEQ BR2 -- TWICE a frame, tested after each.
       *
       * So the ship recedes two units per frame and the loop can end on either half, which is why
       * the exit is not simply "when z_lo wraps on an even frame".
       */
      screen.work[6] = static_cast<std::uint8_t>(screen.work[6] + 1u);
      if (screen.work[6] == 0u)
      {
        break;
      }
      screen.work[6] = static_cast<std::uint8_t>(screen.work[6] + 1u);
      if (screen.work[6] == 0u)
      {
        break;
      }

      // 6502: LDX INWK+3 / INX / CPX #conhieght / BCC P%+4 / LDX #conhieght / STX INWK+3 -- the
      // ship climbs one row a frame and stops at the height the briefing text starts below.
      std::uint8_t height = static_cast<std::uint8_t>(screen.work[3] + 1u);
      if (height >= BRIEFING_SHIP_HEIGHT)
      {
        height = BRIEFING_SHIP_HEIGHT;
      }
      screen.work[3] = height;

      DrawBriefingShip(_mission); // 6502: JSR LL9
      MoveBriefingShip(_mission); // 6502: JSR MVEIT

      screen.flight.mainLoopCounter = static_cast<std::uint8_t>(screen.flight.mainLoopCounter - 1u);
    }

    // 6502: .BR2 INC INWK+7 -- the high byte follows the low one past 255.
    screen.work[7] = static_cast<std::uint8_t>(screen.work[7] + 1u);

    // 6502: LDA #10 / BNE BRPS -- a branch that is a jump, because ten is never zero.
    return MISSION_1_BRIEFING;
  }

  ForcedKey BriefMission1(MissionScreen& _mission, MissionBay& _bay) noexcept
  {
    return PrintAndEnterBay(_mission, _bay, RunConstrictorBriefing(_mission, _bay));
  }

  ForcedKey BriefMission2(MissionScreen& _mission, MissionBay& _bay) noexcept
  {
    // 6502: LDA TP / ORA #%00000100 / STA TP -- in progress, plans not yet collected.
    std::uint8_t& progress = _bay.commander.At(Field::MissionProgress);
    progress = static_cast<std::uint8_t>(progress | MISSION_2_STARTED);

    // 6502: LDA #11 -- and then a FALL-THROUGH into BRP rather than a branch.
    return PrintAndEnterBay(_mission, _bay, MISSION_2_CONTACT);
  }

  ForcedKey CollectPlans(MissionScreen& _mission, MissionBay& _bay) noexcept
  {
    /*
     * 6502: LDA TP / AND #%11110000 / ORA #%00001010 / STA TP.
     *
     * The `AND` clears the low nibble, so picking the plans up also forgets mission 1 entirely --
     * both its bits go, not just the "in progress" one. Bit 1 is then set again by the `ORA`, which
     * is what `MissionOnDocking` reads as "mission 1 finished and paid", and bit 3 is the plans.
     */
    std::uint8_t& progress = _bay.commander.At(Field::MissionProgress);
    progress = static_cast<std::uint8_t>((progress & MISSION_2_KEEP) | MISSION_2_PLANS);

    return PrintAndEnterBay(_mission, _bay, MISSION_2_BRIEFING);
  }

  ForcedKey DebriefMission1(MissionScreen& _mission, MissionBay& _bay) noexcept
  {
    /*
     * 6502: LSR TP / ASL TP -- clear bit 0 and nothing else.
     *
     * Not `AND #%11111110`, and the difference is only in how it reads: the shift pair costs the
     * same four cycles and leaves bit 7 where it was, because the zero the `LSR` shifts in is
     * shifted straight back out by the `ASL`. Bit 1 survives, which is the whole point -- the pair
     * goes from `%11` to `%10` and the mission is never offered again.
     *
     * `\INC TALLY+1` sits between the two halves of this routine, commented out in the original,
     * so the Constrictor is worth no kill points. Not ported, because it does not run.
     */
    std::uint8_t& progress = _bay.commander.At(Field::MissionProgress);
    progress = static_cast<std::uint8_t>(progress & ~MISSION_1_STARTED);

    // 6502: LDX #LO(50000) / LDY #HI(50000) / JSR MCASH -- 5,000 credits.
    ReceiveCash(_bay.commander, MISSION_REWARD);

    // 6502: LDA #15 / .BRPS BNE BRP.
    return PrintAndEnterBay(_mission, _bay, MISSION_1_DEBRIEFING);
  }

  ForcedKey DebriefMission2(MissionScreen& _mission, MissionBay& _bay) noexcept
  {
    // 6502: LDA TP / ORA #%00000100 / STA TP -- bit 2 again, so 2 and 3 are both up and the pair
    // reads as "complete".
    std::uint8_t& progress = _bay.commander.At(Field::MissionProgress);
    progress = static_cast<std::uint8_t>(progress | MISSION_2_STARTED);

    // 6502: LDA #2 / STA ENGY -- the navy's energy unit.
    _bay.commander.At(Field::EnergyUnit) = NAVY_ENERGY_UNIT;

    // 6502: INC TALLY+1 -- 256 kill points, into the HIGH byte, so the low one is untouched and
    // the combat rank jumps by a whole step.
    const std::size_t tally = static_cast<std::size_t>(Field::Kills);
    _bay.commander.bytes[tally + 1u] = static_cast<std::uint8_t>(_bay.commander.bytes[tally + 1u] + 1u);

    return PrintAndEnterBay(_mission, _bay, MISSION_2_DEBRIEFING);
  }

  ForcedKey OfferTrumble(MissionScreen& _mission, MissionBay& _bay, KeySource& _keys) noexcept
  {
    // 6502: LDA TP / ORA #%00010000 / STA TP -- BEFORE the question, so declining still counts as
    // having been asked and the Trumble is never offered again.
    std::uint8_t& progress = _bay.commander.At(Field::MissionProgress);
    progress = static_cast<std::uint8_t>(progress | MISSION_TRUMBLES);

    _mission.tokens.Print(TRUMBLE_OFFER); // 6502: LDA #199 / JSR DETOK

    // 6502: JSR YESNO / BCC BAYSTEP -- "N" goes to the bay WITHOUT printing anything else, which
    // is what `BAYSTEP` is for.
    if (!AskYesNo(_keys))
    {
      return PrintAndEnterBay(_mission, _bay, 0u);
    }

    /*
     * 6502: LDY #HI(50000) / LDX #LO(50000) / JSR LCASH, and the CARRY IS NOT TESTED.
     *
     * `INC TRIBBLE` follows unconditionally and `LCASH` puts the money back when it cannot afford
     * the spend, so a commander who is short gets the Trumble for nothing (ADR-001 §6).
     */
    static_cast<void>(SpendCash(_bay.commander, MISSION_REWARD));

    // 6502: INC TRIBBLE -- the LOW byte, from nothing to one, and `MLOOP` breeds the rest.
    std::uint8_t& trumbles = _bay.commander.At(Field::Tribbles);
    trumbles = static_cast<std::uint8_t>(trumbles + 1u);

    return PrintAndEnterBay(_mission, _bay, 0u); // 6502: JMP BAY
  }

} // namespace Elite
