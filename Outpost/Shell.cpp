#include "pch.h"

#include "Shell.h"

#include "KeyMap.h"

namespace Outpost
{

namespace
{
/// 6502: dn2 -- JSR BEEP / LDY #50 / JMP DELAY.
constexpr std::uint8_t BEEP_PAUSE_FRAMES = 50;

/// 6502: BRIS -- LDA #216 / JSR DETOK / LDY #100 / JMP DELAY.
constexpr std::uint8_t BRIEFING_TOKEN = 216;
constexpr std::uint8_t BRIEFING_FRAMES = 100;

/// 6502: MT23 and MT29 -- the two rows they move to before falling into MT13.
constexpr std::uint8_t MT23_ROW = 10;
constexpr std::uint8_t MT29_ROW = 6;

/// 6502: MT8 -- LDA #6 / JSR DOXC.
constexpr std::uint8_t MT8_COLUMN = 6;

/// 6502: MT9 -- LDA #1 / JSR DOXC / JMP TT66, and STA does not touch A, so TT66 gets the 1 too.
constexpr std::uint8_t MT9_VIEW = 1;

/*
 * Where the title ship would be, until `LL9` exists (slice 3b).
 *
 * The plan's 2e row asks for "the title screen WITHOUT the rotating ship -- a placeholder box
 * until 3b", and a box is what this is. It is centred in the space view and its size follows the
 * distance the way the real projection would, because that is the one thing about it that can be
 * right: `LL9` divides by z, so a Cobra settling at 210 is small and an Adder at 48 fills the
 * screen, and a placeholder of a fixed size would make the two title screens look identical when
 * the game means them to look nothing alike.
 */
constexpr std::uint8_t SPACE_VIEW_CENTRE_X = 128;
constexpr std::uint8_t SPACE_VIEW_CENTRE_Y = Elite::Canvas::SPACE_VIEW_HEIGHT / 2;
constexpr int PLACEHOLDER_PROJECTION = 6000; ///< half-width times distance, so 28 for the Cobra
constexpr int PLACEHOLDER_MIN_HALF_WIDTH = 16;

/// The nearest ship's box would reach row 15 and collide with the prompt, so the placeholder
/// gives way to the text rather than the other way round: half the height is half the width, and
/// 88 puts the bottom edge at y = 116, clear of the prompt's row.
constexpr int PLACEHOLDER_MAX_HALF_WIDTH = 88;

/*
 * 6502: TITLE -- LDA #15 / STA YC / LDA #1 / STA XC, immediately before the PLA and DETOK that
 * print the token this routine was passed.
 *
 * BR1 sets the column to 3 before it calls TITLE and TITLE OVERWRITES IT, which is why the port
 * keeps both: `TITLE_PROMPT_COLUMN` is BR1's store and is pinned against the shipped sequence,
 * and these two are what actually decide where the prompt lands. Getting that wrong is visible
 * rather than academic -- "Press Space Or Fire,Commander." is thirty characters, and from column
 * 3 the last two fall off the 32-column screen and wrap onto the next row.
 */
constexpr std::uint8_t TITLE_PROMPT_ROW = 15;
constexpr std::uint8_t TITLE_PROMPT_COLUMN = 1;

void DrawPlaceholderShip(Elite::Canvas& _canvas, std::uint8_t _distance) noexcept
{
  int halfWidth = (_distance > 0) ? (PLACEHOLDER_PROJECTION / _distance) : PLACEHOLDER_MAX_HALF_WIDTH;
  halfWidth = (halfWidth < PLACEHOLDER_MIN_HALF_WIDTH) ? PLACEHOLDER_MIN_HALF_WIDTH : halfWidth;
  halfWidth = (halfWidth > PLACEHOLDER_MAX_HALF_WIDTH) ? PLACEHOLDER_MAX_HALF_WIDTH : halfWidth;
  const int halfHeight = halfWidth / 2;

  const std::uint8_t left = static_cast<std::uint8_t>(SPACE_VIEW_CENTRE_X - halfWidth);
  const std::uint8_t right = static_cast<std::uint8_t>(SPACE_VIEW_CENTRE_X + halfWidth);
  const std::uint8_t top = static_cast<std::uint8_t>(SPACE_VIEW_CENTRE_Y - halfHeight);
  const std::uint8_t bottom = static_cast<std::uint8_t>(SPACE_VIEW_CENTRE_Y + halfHeight);

  // 6502: LOIN, four times. The line routine alternates between each cell's two colours as it
  // goes, which is why the box comes out striped rather than flat -- and that is the real
  // renderer's behaviour, not an artefact of the placeholder.
  const std::uint8_t corners[4][4] = {
    { left, top, right, top },
    { right, top, right, bottom },
    { right, bottom, left, bottom },
    { left, bottom, left, top },
  };

  Elite::DrawWorkspace work;
  for (const auto& side : corners)
  {
    work.x1 = side[0];
    work.y1 = side[1];
    work.x2 = side[2];
    work.y2 = side[3];
    Elite::DrawLine(_canvas, work);
  }
}
} // namespace

bool GameShell::Turn()
{
  if (!m_window.Pump())
  {
    return false;
  }

  int width = 0;
  int height = 0;
  m_window.ClientSize(width, height);

  if (m_window.TakeResize())
  {
    m_presenter.Resize(width, height);
  }

  /*
   * A minimised window has nothing to present to, and `Present` returns immediately -- so without
   * this the loop would spin a core at whatever rate the pump manages. `WaitMessage` blocks until
   * something arrives, which is the correct idle and is what makes a minimised game cost nothing.
   */
  if (width <= 0 || height <= 0)
  {
    WaitMessage();
    return !m_window.Closed();
  }

  return m_presenter.Present(m_canvas, width, height);
}

std::uint8_t GameShell::NextKey()
{
  std::uint8_t key = 0;
  while (!m_window.TakeKey(key))
  {
    if (!Turn())
    {
      Abandon();
    }
  }

  // 6502: LDA TRANTABLE,X -- the CHARACTER, because that is what TT217 returns in A and what
  // every screen that calls it compares against. The dispatch reads the position instead, and
  // takes it from the window directly.
  return CharacterFor(key);
}

void GameShell::Abandon()
{
  /*
   * The window is gone and the game is somewhere inside a ported routine with no way to be told.
   * Unwinding is not available -- most of `GameLogic` is `noexcept` -- and returning a character
   * would put the caller into a loop that never ends, so the process stops here.
   */
  ExitProcess(0);
  std::terminate(); // not reached; ExitProcess does not return
}

// ---- the screen -------------------------------------------------------------------------------

void GameShell::SetUpTradeScreen(std::uint8_t _view)
{
  // 6502: TRADEMODE -- TT66, then FLKB, then DOVDU19 (a palette change this build does not act on).
  ClearToView(_view);
  FlushKeyboard();
}

void GameShell::ClearToView(std::uint8_t _view)
{
  m_view = _view; // 6502: STA QQ11

  if (m_text == nullptr || m_printer == nullptr || m_extended == nullptr)
  {
    return;
  }

  /*
   * 6502: TTX66K's palette fill and then its text-area clear.
   *
   * The fill comes FIRST because that is the order TTX66K does it in, and it has to happen at all
   * because the bitmap holds two-bit codes rather than colours: without a palette byte behind each
   * cell every code resolves to colour 0, and the screen draws black on black. The port has
   * TT66simp for the clear itself, which is the same 32 cells of rows 1 to 23; the dashboard, the
   * sprites, the border box and the colour bands are phase 3's, so a cleared screen here has no
   * border yet.
   */
  Elite::ResetCellColours(m_canvas);
  Elite::ClearTextArea(m_canvas, *m_text);
  Elite::SetUpTextScreen(*m_printer, *m_text, *m_extended);
}

void GameShell::ClearBottomRows()
{
  if (m_text == nullptr || m_printer == nullptr || m_extended == nullptr || m_message == nullptr)
  {
    return;
  }
  Elite::ClearMessageRows(m_canvas, *m_printer, *m_text, *m_extended, *m_message);
}

void GameShell::ClearScreen()
{
  // 6502: clss -- CHPR reaching past the last row clears the screen and prints again. The caller
  // does the printing; this is the clear.
  ClearToView(m_view);
}

void GameShell::BeepAndPause()
{
  Beep();
  WaitFrames(BEEP_PAUSE_FRAMES);
}

void GameShell::Beep()
{
  // 6502: BEEP. Phase 5 owns the SID; until then a refused key is silent, which is the one
  // stubbed effect a player is most likely to notice.
}

void GameShell::ResetMissileIndicators()
{
  // 6502: msblob. The dashboard is phase 3's.
}

// ---- waiting and the keyboard ------------------------------------------------------------------

void GameShell::WaitFrames(std::uint8_t _frames)
{
  /*
   * 6502: DELAY -- wait for _frames VERTICAL SYNCS, and that is literally what this is: `Turn`
   * ends in `Present(1, 0)`, so a turn is a frame. No timer, no sleep, and the wait is the same
   * length as the original's on a 50 Hz display and shorter on a 60 Hz one -- which is the PAL
   * and NTSC difference section 6.17 records rather than a defect in this loop.
   */
  for (std::uint8_t frame = 0; frame < _frames; ++frame)
  {
    if (!Turn())
    {
      Abandon();
    }
  }
}

void GameShell::FlushKeyboard()
{
  m_window.FlushKeys(); // 6502: FLKB
}

void GameShell::ClearKeyLogger()
{
  m_window.FlushKeys(); // 6502: ZEKTRAN -- sixty-five bytes of KEYLOOK and `thiskey`
}

// ---- the start sequence -------------------------------------------------------------------------

void GameShell::ResetUniverse()
{
  // 6502: RESET, which falls into RES2. The ship slots and the stardust are phase 3's, so what
  // is left of RESET here is the fall-through itself.
  ResetShip();
}

void GameShell::ResetShip()
{
  // 6502: RES2 -- LDA #&10 / STA COL2, "switch the text colour to white". The ship slots, the
  // stardust and dontclip are phase 3's; the text colour is not, and a screen printed without it
  // is printed in black on black.
  if (m_text != nullptr)
  {
    m_text->cellColour = Elite::TEXT_COLOUR_WHITE;
  }
}

void GameShell::StartTheme()
{
  // 6502: startat. Phase 5.
}

void GameShell::StopTheme()
{
  // 6502: stopat. Phase 5.
}

void GameShell::ShowDockingTunnel()
{
  // 6502: LAUN -- expanding circles. Phase 3c owns the line heap they are drawn through.
}

std::uint8_t GameShell::ShowTitleScreen(std::uint8_t _token, std::uint8_t _shipType, std::uint8_t _distance)
{
  /*
   * 6502: TITLE. What is here is the token and the key; what is not is the ROTATING SHIP, which
   * is `LL9` and lands in slice 3b. The plan's 2e row names that omission, and it is the reason
   * this slice's acceptance is a docked game rather than a finished front end.
   */
  (void)_shipType; // 6502: the ship BLUEPRINT, which needs the ship data slice 3a extracts.

  ClearToView(0);
  DrawPlaceholderShip(m_canvas, _distance);

  if (m_extendedPrinter != nullptr && m_text != nullptr)
  {
    // 6502: LDA #15 / STA YC / LDA #1 / STA XC -- TITLE's own cursor, not BR1's.
    m_text->row = TITLE_PROMPT_ROW;
    m_text->column = TITLE_PROMPT_COLUMN;
    m_extendedPrinter->Print(_token);
  }

  return NextKey();
}

// ---- the control codes that leave the text system ------------------------------------------------

void GameShell::Run(std::uint8_t _code)
{
  switch (_code)
  {
    case 8:
      // 6502: MT8 -- LDA #6 / JSR DOXC. The DTW2 store is the printer's and is already done.
      if (m_text != nullptr)
      {
        m_text->column = MT8_COLUMN;
      }
      return;

    case 9:
      // 6502: MT9 -- LDA #1 / JSR DOXC / JMP TT66.
      ClearToView(MT9_VIEW);
      return;

    case 21:
      // 6502: CLYNS. The two flags are the printer's and are already set.
      ClearBottomRows();
      return;

    case 23:
    case 29:
      // 6502: MT23 and MT29 -- one routine, two entries, and the row is the only difference.
      // WHITETEXT is an RTS in this build, and MT13's stores are the printer's.
      if (m_text != nullptr)
      {
        m_text->row = (_code == 23) ? MT23_ROW : MT29_ROW;
        m_text->column = 1;
      }
      return;

    case 22:
    case 24:
      /*
       * 6502: PAUSE and PAUSE2 -- spin the title ship and wait for a key. The ship is `LL9` and
       * is phase 3b's; the WAIT is not, and doing it is the difference between a briefing screen
       * a player can dismiss and one the game runs straight past.
       */
      (void)NextKey();
      return;

    case 25:
      // 6502: BRIS -- LDA #216 / JSR DETOK / LDY #100 / JMP DELAY.
      if (m_extendedPrinter != nullptr)
      {
        m_extendedPrinter->Print(BRIEFING_TOKEN);
      }
      WaitFrames(BRIEFING_FRAMES);
      return;

    default:
      /*
       * 11 (`NLIN4`, a rule across the screen) is phase 3's, with the border box it belongs to.
       *
       * 26 (`MT26`, read a line) and 27, 28, 30, 31 (tokens under `GCNT` and `DISK`) are reached
       * only from the MISSION briefings, which are phase 4. `MT26` is ported and could be called
       * from here; what it has no answer for yet is whose buffer the line goes into, and the
       * mission that reads it is the thing that would say.
       */
      return;
  }
}

} // namespace Outpost
