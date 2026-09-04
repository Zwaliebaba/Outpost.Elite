#pragma once

#include "CanvasPresenter.h"
#include "Window.h"

#include "Canvas.h"
#include "Charts.h"
#include "ExtendedTokens.h"
#include "MarketScreen.h"
#include "NameEntry.h"
#include "StartUp.h"
#include "TextPrint.h"
#include "Tokens.h"

#include <cstdint>

namespace Outpost
{

/*
 * Everything the game reaches for outside `GameLogic`, answered once (slice 2e).
 *
 * SEVEN interfaces on one object, which is what ADR-004 says the executable is: each screen
 * declares what it needs separately and the shell answers all of it. `DockedSessionTests.cpp`
 * builds the same shape out of a null presenter and asserts that the declarations are mutually
 * consistent, so the arrangement here is verified before this file compiles.
 *
 * Three of the methods below appear on two interfaces each -- `ClearBottomRows` on the trade
 * screens and the charts, `WaitFrames` on the line editor and the start sequence,
 * `ResetMissileIndicators` on the trade screens and the start sequence. One definition overrides
 * both in each case, which is the language's own rule and is deliberate rather than lucky: two
 * independent statements of what a routine needs, satisfied by one thing.
 *
 * WHAT IS HONESTLY MISSING, and it is said here rather than left to be discovered while playing.
 * Phase 3 owns the flight model, the dashboard and the ship renderer; phase 5 owns sound. Every
 * method whose body is a comment saying so is a routine that exists in the game and does not
 * exist here yet, and the ones a player would notice -- the title ship, the docking tunnel, the
 * border box -- are the reason 2e's acceptance is a DOCKED game rather than a complete one.
 */
class GameShell final : public Elite::TradeScreenEffects,
                        public Elite::ChartEffects,
                        public Elite::LineEntryEffects,
                        public Elite::StartUpEffects,
                        public Elite::ControlCodes,
                        public Elite::TextEffects,
                        public Elite::KeySource
{
public:
  GameShell(Window& _window, CanvasPresenter& _presenter, Elite::Canvas& _canvas) noexcept
    : m_window(_window)
    , m_presenter(_presenter)
    , m_canvas(_canvas)
  {
  }

  /// The text system the shell drives, wired up by the composition root once it exists. The
  /// message counters come with it because `CLYNS` clears them (§6.67).
  void Attach(Elite::TokenPrinter& _printer, Elite::TextState& _text,
              Elite::ExtendedTextState& _extended, Elite::MessageState& _message) noexcept
  {
    m_printer = &_printer;
    m_text = &_text;
    m_extended = &_extended;
    m_message = &_message;
  }

  /*
   * One turn of the outer loop: dispatch what the window has, then draw and wait for the vertical
   * blank. Returns false once the window has closed.
   *
   * This is the whole of the shell's timing. There is no sleep and no timer anywhere in the
   * program: `Present` blocks on vsync, so a turn of this is a frame, and `WaitFrames` below is
   * literally the C64's `DELAY` -- a count of vertical syncs.
   */
  [[nodiscard]] bool Turn();

  // ---- Elite::KeySource ----------------------------------------------------------------------

  /*
   * 6502: TT217 -- block until a key is pressed.
   *
   * The nested pump `Window.h` argues for. The window stays alive while the game waits, and the
   * canvas cannot be uploaded mid-mutation because the code that mutates it is the code that is
   * not running while this is.
   *
   * ON CLOSE IT DOES NOT RETURN. That is the price of a blocking seam and it is chosen rather
   * than stumbled into: `NextKey` has no error channel, and every value it could return is a
   * character some caller will act on -- zero is below the line editor's lowest accepted
   * character, so returning it would put `MT26` into an unbounded beep. Handing control back into
   * a routine that cannot be told the game is over is worse than not handing it back, so this
   * ends the process where it stands. Nothing is lost: the commander is on disk or it is not, and
   * a save is a menu item rather than a shutdown hook.
   */
  std::uint8_t NextKey() override;

  // ---- Elite::TradeScreenEffects and Elite::ChartEffects -------------------------------------

  void SetUpTradeScreen(std::uint8_t _view) override;
  void ClearToView(std::uint8_t _view) override;
  void ClearBottomRows() override;
  void BeepAndPause() override;
  void ResetMissileIndicators() override;

  // ---- Elite::LineEntryEffects and Elite::StartUpEffects --------------------------------------

  void WaitFrames(std::uint8_t _frames) override;
  void FlushKeyboard() override;

  void ResetUniverse() override;
  void ResetShip() override;
  void ClearKeyLogger() override;
  void StartTheme() override;
  void StopTheme() override;
  void ShowDockingTunnel() override;
  [[nodiscard]] std::uint8_t ShowTitleScreen(std::uint8_t _token, std::uint8_t _shipType,
                                             std::uint8_t _distance) override;

  // ---- Elite::ControlCodes and Elite::TextEffects ---------------------------------------------

  void Run(std::uint8_t _code) override;
  void Beep() override;
  void ClearScreen() override;

  /// 6502: QQ11 -- which screen is showing. The shell owns it because `TT66` is the only thing
  /// that writes it and `TT66` is a seam.
  [[nodiscard]] std::uint8_t View() const noexcept { return m_view; }

  /// The extended token printer, for the control codes that print. Set by the composition root
  /// after construction, because the printer needs this object to exist first.
  void AttachExtended(Elite::ExtendedTokenPrinter& _extendedPrinter) noexcept
  {
    m_extendedPrinter = &_extendedPrinter;
  }

private:
  /// Ends the process. See `NextKey`.
  [[noreturn]] void Abandon();

  Window& m_window;
  CanvasPresenter& m_presenter;
  Elite::Canvas& m_canvas;

  Elite::TokenPrinter* m_printer = nullptr;
  Elite::TextState* m_text = nullptr;
  Elite::ExtendedTextState* m_extended = nullptr;
  Elite::MessageState* m_message = nullptr;
  Elite::ExtendedTokenPrinter* m_extendedPrinter = nullptr;

  std::uint8_t m_view = 0; ///< 6502: QQ11
};

} // namespace Outpost
