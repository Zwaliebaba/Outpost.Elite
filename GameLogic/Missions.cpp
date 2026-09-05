#include "pch.h"

#include "Missions.h"

#include "EliteTypes.h"
#include "ShipDraw.h"
#include "ShipMove.h"
#include "ViewChange.h"

namespace Elite
{

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
    DrawShip(screen.canvas, screen.draw, screen.geometry, screen.math, loop.clip, loop.projection, screen.work,
             screen.bubble.blocks[_mission.shipSlot], loop.heap, screen.flight.blueprint, screen.flight.type, loop.drawing);

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

} // namespace Elite
